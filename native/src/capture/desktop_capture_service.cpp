#include "capture/desktop_capture_service.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QColorSpace>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

#include "platform/windows/native_screen_capture.h"

namespace ais::capture {

namespace {

[[nodiscard]] bool isHdrLikeFormat(const QImage::Format format) {
    switch (format) {
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isHdrCandidate(const QImage& image, const QColorSpace& sourceColorSpace) {
    return isHdrLikeFormat(image.format()) ||
           sourceColorSpace.transferFunction() == QColorSpace::TransferFunction::Linear ||
           sourceColorSpace.transferFunction() == QColorSpace::TransferFunction::Custom;
}

struct LinearImageStats {
    float peakLuminance = 0.0f;
    float p95Luminance = 0.0f;
    float medianLuminance = 0.0f;
    float brightPixelShare = 0.0f;
    float darkPixelShare = 0.0f;
};

[[nodiscard]] float linearLuminance(const float red, const float green, const float blue) {
    return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

[[nodiscard]] LinearImageStats analyzeLinearImage(const QImage& linearImage) {
    LinearImageStats stats;
    if (linearImage.isNull() || linearImage.format() != QImage::Format_RGBA32FPx4) {
        return stats;
    }

    const int xStep = qMax(1, linearImage.width() / 96);
    const int yStep = qMax(1, linearImage.height() / 96);
    std::vector<float> luminances;
    luminances.reserve((linearImage.width() / xStep + 1) * (linearImage.height() / yStep + 1));

    int brightSamples = 0;
    int darkSamples = 0;
    for (int y = 0; y < linearImage.height(); y += yStep) {
        const auto* row = reinterpret_cast<const float*>(linearImage.constScanLine(y));
        for (int x = 0; x < linearImage.width(); x += xStep) {
            const float red = row[x * 4 + 0];
            const float green = row[x * 4 + 1];
            const float blue = row[x * 4 + 2];
            const float luminance = linearLuminance(red, green, blue);
            stats.peakLuminance = std::max(stats.peakLuminance, luminance);
            if (luminance >= 0.985f) {
                ++brightSamples;
            }
            if (luminance <= 0.35f) {
                ++darkSamples;
            }
            luminances.push_back(luminance);
        }
    }

    if (luminances.empty()) {
        return stats;
    }

    stats.brightPixelShare =
        static_cast<float>(brightSamples) / static_cast<float>(luminances.size());
    stats.darkPixelShare =
        static_cast<float>(darkSamples) / static_cast<float>(luminances.size());

    const std::size_t percentileIndex = std::min<std::size_t>(
        luminances.size() - 1,
        static_cast<std::size_t>(std::floor((luminances.size() - 1) * 0.95)));
    std::nth_element(luminances.begin(),
                     luminances.begin() + static_cast<std::ptrdiff_t>(percentileIndex),
                     luminances.end());
    stats.p95Luminance = luminances[percentileIndex];

    const std::size_t medianIndex = luminances.size() / 2;
    std::nth_element(luminances.begin(),
                     luminances.begin() + static_cast<std::ptrdiff_t>(medianIndex),
                     luminances.end());
    stats.medianLuminance = luminances[medianIndex];
    return stats;
}

[[nodiscard]] QColorSpace linearColorSpaceFor(const QColorSpace& sourceColorSpace) {
    if (sourceColorSpace.transferFunction() == QColorSpace::TransferFunction::Linear) {
        return sourceColorSpace;
    }

    const QColorSpace linearColorSpace =
        sourceColorSpace.withTransferFunction(QColorSpace::TransferFunction::Linear);
    return linearColorSpace.isValid() ? linearColorSpace : QColorSpace(QColorSpace::SRgbLinear);
}

void applyHighlightCompression(QImage* linearImage,
                               const float exposureScale,
                               const float highlightStart,
                               const float highlightRollOff) {
    if (linearImage == nullptr || linearImage->isNull() ||
        linearImage->format() != QImage::Format_RGBA32FPx4) {
        return;
    }

    for (int y = 0; y < linearImage->height(); ++y) {
        auto* row = reinterpret_cast<float*>(linearImage->scanLine(y));
        for (int x = 0; x < linearImage->width(); ++x) {
            float& red = row[x * 4 + 0];
            float& green = row[x * 4 + 1];
            float& blue = row[x * 4 + 2];
            float& alpha = row[x * 4 + 3];

            red = std::max(0.0f, red * exposureScale);
            green = std::max(0.0f, green * exposureScale);
            blue = std::max(0.0f, blue * exposureScale);
            alpha = std::clamp(alpha, 0.0f, 1.0f);

            const float luminance = linearLuminance(red, green, blue);
            if (luminance <= highlightStart) {
                continue;
            }

            const float delta = luminance - highlightStart;
            const float compressedLuminance =
                highlightStart + delta / (1.0f + delta / highlightRollOff);
            const float scale = compressedLuminance / std::max(luminance, 0.0001f);
            red *= scale;
            green *= scale;
            blue *= scale;
        }
    }
}

void applyHdrToneMapping(QImage* linearImage, const LinearImageStats& stats) {
    constexpr float kHdrDetectionThreshold = 1.02f;
    if (linearImage == nullptr || linearImage->isNull() ||
        stats.p95Luminance <= kHdrDetectionThreshold && stats.peakLuminance <= kHdrDetectionThreshold) {
        return;
    }

    applyHighlightCompression(linearImage,
                              1.0f / std::max(1.0f, stats.p95Luminance),
                              0.82f,
                              0.22f);
}

[[nodiscard]] bool shouldCompressClippedSdrHighlights(const LinearImageStats& stats) {
    return stats.peakLuminance >= 0.995f &&
           stats.p95Luminance >= 0.97f &&
           stats.brightPixelShare >= 0.35f &&
           stats.darkPixelShare >= 0.08f;
}

[[nodiscard]] QImage toneMappedHdrToSdr(const QImage& image, const QColorSpace& sourceColorSpace) {
    QImage linearImage = image;
    const QColorSpace linearColorSpace = linearColorSpaceFor(sourceColorSpace);

    if (linearImage.colorSpace() != linearColorSpace) {
        linearImage = linearImage.convertedToColorSpace(linearColorSpace);
    }
    if (linearImage.format() != QImage::Format_RGBA32FPx4) {
        linearImage = linearImage.convertToFormat(QImage::Format_RGBA32FPx4);
    }
    if (!linearImage.colorSpace().isValid()) {
        linearImage.setColorSpace(linearColorSpace);
    }

    applyHdrToneMapping(&linearImage, analyzeLinearImage(linearImage));

    QImage srgbImage = linearImage.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    if (srgbImage.isNull()) {
        srgbImage = linearImage;
        srgbImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return srgbImage;
}

[[nodiscard]] QImage compressedClippedSdrToSdr(const QImage& image,
                                               const QColorSpace& sourceColorSpace) {
    QImage linearImage = image;
    const QColorSpace linearColorSpace = linearColorSpaceFor(sourceColorSpace);

    if (linearImage.colorSpace() != linearColorSpace) {
        linearImage = linearImage.convertedToColorSpace(linearColorSpace);
    }
    if (linearImage.format() != QImage::Format_RGBA32FPx4) {
        linearImage = linearImage.convertToFormat(QImage::Format_RGBA32FPx4);
    }
    if (!linearImage.colorSpace().isValid()) {
        linearImage.setColorSpace(linearColorSpace);
    }

    const LinearImageStats stats = analyzeLinearImage(linearImage);
    if (!shouldCompressClippedSdrHighlights(stats)) {
        QImage srgbImage = image;
        if (srgbImage.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
            srgbImage = srgbImage.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
        }
        return srgbImage;
    }

    applyHighlightCompression(&linearImage, 1.0f, 0.88f, 0.16f);

    QImage srgbImage = linearImage.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    if (srgbImage.isNull()) {
        srgbImage = linearImage;
        srgbImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return srgbImage;
}

[[nodiscard]] QList<CapturedScreenFrame> capturedFramesFromScreens(const QList<QScreen*>& screens) {
    QList<CapturedScreenFrame> frames;

    for (QScreen* screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const QPixmap shot = screen->grabWindow(0);
        if (shot.isNull() || shot.width() <= 0 || shot.height() <= 0) {
            continue;
        }

        frames.append(CapturedScreenFrame{
            .image = DesktopCaptureService::normalizeForSdr(shot.toImage()),
            .overlayGeometry = screen->geometry(),
            .virtualGeometry = screen->geometry(),
            .devicePixelRatio = 1.0,
        });
    }

    return frames;
}

[[nodiscard]] QString normalizedScreenName(QString name) {
    name = name.trimmed().toUpper();
    if (name.startsWith(QStringLiteral(R"(\\?\)"))) {
        name.remove(0, 4);
    }
    if (name.startsWith(QStringLiteral(R"(\\.\)"))) {
        name.remove(0, 4);
    }
    return name;
}

[[nodiscard]] QList<CapturedScreenFrame> capturedFramesFromNativeScreens(const QList<QScreen*>& screens) {
    const QList<platform::windows::NativeScreenFrame> nativeScreens =
        platform::windows::captureNativeScreens();
    if (nativeScreens.isEmpty()) {
        return {};
    }

    QHash<QString, platform::windows::NativeScreenFrame> nativeScreensByName;
    for (const auto& nativeScreen : nativeScreens) {
        if (!nativeScreen.image.isNull()) {
            nativeScreensByName.insert(normalizedScreenName(nativeScreen.deviceName), nativeScreen);
        }
    }

    QList<CapturedScreenFrame> frames;
    for (QScreen* screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const auto nativeScreen = nativeScreensByName.value(normalizedScreenName(screen->name()));
        if (nativeScreen.image.isNull()) {
            continue;
        }

        frames.append(CapturedScreenFrame{
            .image = DesktopCaptureService::normalizeForSdr(nativeScreen.image),
            .overlayGeometry = nativeScreen.monitorRect,
            .virtualGeometry = screen->geometry(),
            .devicePixelRatio = qMax(1.0, screen->devicePixelRatio()),
        });
    }

    return frames;
}

}  // namespace

DesktopSnapshot DesktopCaptureService::captureVirtualDesktop() const {
    const QList<QScreen*> screens = QGuiApplication::screens();
    const QList<CapturedScreenFrame> nativeFrames = capturedFramesFromNativeScreens(screens);
    if (!nativeFrames.isEmpty() && nativeFrames.size() == screens.size()) {
        return composeFrames(nativeFrames);
    }

    return composeFrames(capturedFramesFromScreens(screens));
}

DesktopSnapshot DesktopCaptureService::composeFrames(const QList<CapturedScreenFrame>& frames) {
    QRect overlayGeometry;
    QRect virtualGeometry;
    bool first = true;
    QList<CapturedScreenFrame> validFrames;
    QList<ScreenMapping> screenMappings;

    for (const CapturedScreenFrame& frame : frames) {
        if (frame.image.isNull() || !frame.overlayGeometry.isValid() || frame.overlayGeometry.isEmpty()) {
            continue;
        }

        validFrames.append(frame);
        screenMappings.append(ScreenMapping{
            .overlayRect = frame.overlayGeometry,
            .virtualRect = frame.virtualGeometry.isValid() ? frame.virtualGeometry : frame.overlayGeometry,
        });
        overlayGeometry = first ? frame.overlayGeometry : overlayGeometry.united(frame.overlayGeometry);
        const QRect frameVirtualGeometry =
            frame.virtualGeometry.isValid() ? frame.virtualGeometry : frame.overlayGeometry;
        virtualGeometry = first ? frameVirtualGeometry : virtualGeometry.united(frameVirtualGeometry);
        first = false;
    }

    if (validFrames.isEmpty() || overlayGeometry.isNull() || overlayGeometry.isEmpty()) {
        return DesktopSnapshot{};
    }

    QImage canvas(QSize(qMax(1, overlayGeometry.width()),
                        qMax(1, overlayGeometry.height())),
                  QImage::Format_ARGB32_Premultiplied);
    canvas.setColorSpace(QColorSpace(QColorSpace::SRgb));
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const CapturedScreenFrame& frame : validFrames) {
        const QPoint topLeft = frame.overlayGeometry.topLeft() - overlayGeometry.topLeft();
        const QRect targetRect(topLeft, frame.overlayGeometry.size());
        painter.drawImage(targetRect, normalizeForSdr(frame.image));
    }
    painter.end();

    QPixmap stitched = QPixmap::fromImage(canvas);
    return DesktopSnapshot{
        .displayImage = stitched,
        .captureImage = stitched,
        .overlayGeometry = overlayGeometry,
        .virtualGeometry = virtualGeometry,
        .screenMappings = screenMappings,
    };
}

QImage DesktopCaptureService::normalizeForSdr(const QImage& image) {
    if (image.isNull()) {
        return {};
    }

    QImage normalized = image;
    QColorSpace sourceColorSpace = normalized.colorSpace();
    if (!sourceColorSpace.isValid()) {
        sourceColorSpace = QColorSpace(isHdrLikeFormat(normalized.format())
                                           ? QColorSpace::SRgbLinear
                                           : QColorSpace::SRgb);
        normalized.setColorSpace(sourceColorSpace);
    }

    if (isHdrCandidate(normalized, sourceColorSpace)) {
        normalized = toneMappedHdrToSdr(normalized, sourceColorSpace);
    } else {
        normalized = compressedClippedSdrToSdr(normalized, sourceColorSpace);
        if (normalized.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
            normalized = normalized.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
        }
    }

    if (normalized.format() != QImage::Format_ARGB32_Premultiplied &&
        normalized.format() != QImage::Format_RGB32 &&
        normalized.format() != QImage::Format_RGBA8888_Premultiplied) {
        normalized = normalized.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    if (!normalized.colorSpace().isValid()) {
        normalized.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }

    return normalized;
}

QRect DesktopCaptureService::translateToVirtual(const QRect& localRect,
                                                const QPoint& virtualOrigin) {
    return localRect.translated(virtualOrigin);
}

QRect DesktopCaptureService::translateToVirtual(const DesktopSnapshot& snapshot,
                                                const QRect& localRect) {
    if (!localRect.isValid() || localRect.isEmpty()) {
        return {};
    }

    if (snapshot.screenMappings.isEmpty() || !snapshot.overlayGeometry.isValid()) {
        return translateToVirtual(localRect, snapshot.virtualGeometry.topLeft());
    }

    const QRect globalOverlayRect = localRect.translated(snapshot.overlayGeometry.topLeft());
    QRect mappedVirtualRect;
    bool first = true;

    for (const ScreenMapping& mapping : snapshot.screenMappings) {
        const QRect overlayRect = mapping.overlayRect;
        const QRect virtualRect = mapping.virtualRect.isValid() ? mapping.virtualRect : mapping.overlayRect;
        const QRect intersection = globalOverlayRect.intersected(overlayRect);
        if (!intersection.isValid() || intersection.isEmpty()) {
            continue;
        }

        const double scaleX = overlayRect.width() > 0
                                  ? static_cast<double>(virtualRect.width()) /
                                        static_cast<double>(overlayRect.width())
                                  : 1.0;
        const double scaleY = overlayRect.height() > 0
                                  ? static_cast<double>(virtualRect.height()) /
                                        static_cast<double>(overlayRect.height())
                                  : 1.0;

        const int relativeLeft = intersection.left() - overlayRect.left();
        const int relativeTop = intersection.top() - overlayRect.top();
        const int mappedLeft = virtualRect.left() + qRound(relativeLeft * scaleX);
        const int mappedTop = virtualRect.top() + qRound(relativeTop * scaleY);
        const int mappedWidth = qMax(1, qRound(intersection.width() * scaleX));
        const int mappedHeight = qMax(1, qRound(intersection.height() * scaleY));
        const QRect mappedRect(mappedLeft, mappedTop, mappedWidth, mappedHeight);

        mappedVirtualRect = first ? mappedRect : mappedVirtualRect.united(mappedRect);
        first = false;
    }

    if (!first) {
        return mappedVirtualRect;
    }

    return translateToVirtual(localRect, snapshot.virtualGeometry.topLeft());
}

QPixmap DesktopCaptureService::copyLogicalSelection(const QPixmap& source,
                                                    const QRect& logicalRect) {
    if (source.isNull() || !logicalRect.isValid() || logicalRect.isEmpty()) {
        return {};
    }

    const qreal devicePixelRatio = qMax(1.0, source.devicePixelRatio());
    const QRect physicalRect(qRound(logicalRect.x() * devicePixelRatio),
                             qRound(logicalRect.y() * devicePixelRatio),
                             qRound(logicalRect.width() * devicePixelRatio),
                             qRound(logicalRect.height() * devicePixelRatio));

    QPixmap cropped = source.copy(physicalRect);
    cropped = QPixmap::fromImage(normalizeForSdr(cropped.toImage()));
    cropped.setDevicePixelRatio(devicePixelRatio);
    return cropped;
}

}  // namespace ais::capture
