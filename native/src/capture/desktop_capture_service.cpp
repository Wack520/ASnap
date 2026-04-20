#include "capture/desktop_capture_service.h"

#include <QColorSpace>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

#include "capture/frame_normalizer.h"
#include "platform/windows/native_screen_capture.h"

namespace ais::capture {

namespace {

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
    return FrameNormalizer::normalizeToSdr(image);
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
