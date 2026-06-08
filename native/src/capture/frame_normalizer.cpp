#include "capture/frame_normalizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QColorSpace>

namespace ais::capture {

namespace {

struct LinearImageStats {
    float peakLuminance = 0.0f;
    float p95Luminance = 0.0f;
    float medianLuminance = 0.0f;
    float brightPixelShare = 0.0f;
    float darkPixelShare = 0.0f;
};

struct NormalizedFrame {
    QImage image;
    bool hdrToneMapped = false;
};

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
    for (int y = 0; y < linearImage.height(); ++y) {
        const auto* row = reinterpret_cast<const float*>(linearImage.constScanLine(y));
        const bool sampleRow = (y % yStep) == 0;
        for (int x = 0; x < linearImage.width(); ++x) {
            const float red = row[x * 4 + 0];
            const float green = row[x * 4 + 1];
            const float blue = row[x * 4 + 2];
            const float luminance = linearLuminance(red, green, blue);
            stats.peakLuminance = std::max(stats.peakLuminance, luminance);
            if (!sampleRow || (x % xStep) != 0) {
                continue;
            }
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

[[nodiscard]] bool shouldToneMapHdr(const LinearImageStats& stats) {
    constexpr float kHdrDetectionThreshold = 1.02f;
    return stats.p95Luminance > kHdrDetectionThreshold ||
           stats.peakLuminance > kHdrDetectionThreshold;
}

void applyHdrToneMapping(QImage* linearImage, const LinearImageStats& stats) {
    if (linearImage == nullptr || linearImage->isNull() || !shouldToneMapHdr(stats)) {
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

[[nodiscard]] NormalizedFrame toneMappedHdrToSdr(const QImage& image,
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
    const bool hdrToneMapped = shouldToneMapHdr(stats);
    applyHdrToneMapping(&linearImage, stats);

    QImage srgbImage = linearImage.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    if (srgbImage.isNull()) {
        srgbImage = linearImage;
        srgbImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return {
        .image = srgbImage,
        .hdrToneMapped = hdrToneMapped,
    };
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

[[nodiscard]] NormalizedFrame normalizeToSdrInternal(const QImage& image,
                                                     const bool forceHdrLike) {
    if (image.isNull()) {
        return {};
    }

    QImage normalized = image;
    QColorSpace sourceColorSpace = normalized.colorSpace();
    if (!sourceColorSpace.isValid()) {
        sourceColorSpace = QColorSpace(forceHdrLike || isHdrLikeFormat(normalized.format())
                                           ? QColorSpace::SRgbLinear
                                           : QColorSpace::SRgb);
        normalized.setColorSpace(sourceColorSpace);
    }

    bool hdrToneMapped = false;
    if (forceHdrLike || isHdrCandidate(normalized, sourceColorSpace)) {
        const NormalizedFrame hdrNormalized = toneMappedHdrToSdr(normalized, sourceColorSpace);
        normalized = hdrNormalized.image;
        hdrToneMapped = hdrNormalized.hdrToneMapped;
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

    return {
        .image = normalized,
        .hdrToneMapped = hdrToneMapped,
    };
}

[[nodiscard]] QImage imageWithFrameColorSpace(const RawScreenFrame& frame) {
    QImage image = frame.image;
    if (frame.colorSpace.isValid()) {
        image.setColorSpace(frame.colorSpace);
    }
    return image;
}

}  // namespace

QImage FrameNormalizer::normalizeToSdr(const QImage& image) {
    return normalizeToSdrInternal(image, false).image;
}

PreparedScreenFrame FrameNormalizer::normalizeFrame(const RawScreenFrame& frame) {
    const QImage image = imageWithFrameColorSpace(frame);
    const NormalizedFrame normalized = normalizeToSdrInternal(image, frame.isHdrLike);
    return {
        .display = frame.display,
        .normalizedImage = normalized.image,
        .backendKind = frame.backendKind,
        .hdrToneMapped = normalized.hdrToneMapped,
    };
}

}  // namespace ais::capture
