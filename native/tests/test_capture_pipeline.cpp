#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QRect>
#include <QString>
#include <QTest>

#include "capture/capture_pipeline_types.h"
#include "capture/frame_normalizer.h"

class CapturePipelineTests final : public QObject {
    Q_OBJECT

private slots:
    void capturePipelineTypesExposeStableDefaults();
    void hdrLikeImageIsNormalizedToSdrColorSpace();
    void hdrLikeImageWithoutMetadataAssumesLinearSdrConversion();
    void chromeLikeHdrWhitesAreCompressedBeforeSdrClipping();
    void clippedSdrChromeLikeHighlightsAreCompressed();
};

void CapturePipelineTests::capturePipelineTypesExposeStableDefaults() {
    using namespace ais::capture;

    const DisplayDescriptor defaultDisplay;
    const DisplayDescriptor configuredDisplay{
        .deviceName = QStringLiteral(R"(\\.\DISPLAY1)"),
        .monitorRect = QRect(0, 0, 1920, 1080),
        .virtualRect = QRect(0, 0, 1536, 864),
        .devicePixelRatio = 1.25,
        .isPrimary = true,
    };
    const CaptureDiagnostics diagnostics;

    QCOMPARE(defaultDisplay.devicePixelRatio, 1.0);
    QVERIFY(!defaultDisplay.isPrimary);
    QVERIFY(diagnostics.entries.isEmpty());

    QCOMPARE(configuredDisplay.devicePixelRatio, 1.25);
    QVERIFY(configuredDisplay.isPrimary);
    QVERIFY(diagnostics.entries.isEmpty());
}

void CapturePipelineTests::hdrLikeImageIsNormalizedToSdrColorSpace() {
    QImage hdrLike(QSize(32, 24), QImage::Format_RGBA16FPx4_Premultiplied);
    hdrLike.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    hdrLike.fill(Qt::white);

    const QImage normalized = ais::capture::FrameNormalizer::normalizeToSdr(hdrLike);

    QVERIFY(normalized.colorSpace().isValid());
    QCOMPARE(normalized.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(normalized.format() == QImage::Format_ARGB32_Premultiplied ||
             normalized.format() == QImage::Format_RGBA8888_Premultiplied ||
             normalized.format() == QImage::Format_RGB32);
}

void CapturePipelineTests::hdrLikeImageWithoutMetadataAssumesLinearSdrConversion() {
    QImage hdrLike(QSize(16, 16), QImage::Format_RGBA16FPx4_Premultiplied);
    hdrLike.fill(QColor::fromRgbF(0.5f, 0.5f, 0.5f, 1.0f));

    const QImage normalized = ais::capture::FrameNormalizer::normalizeToSdr(hdrLike);

    QVERIFY(normalized.colorSpace().isValid());
    QCOMPARE(normalized.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(normalized.pixelColor(4, 4).red() > 170);
}

void CapturePipelineTests::chromeLikeHdrWhitesAreCompressedBeforeSdrClipping() {
    QImage chromeLike(QSize(24, 24), QImage::Format_RGBA32FPx4);
    chromeLike.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    chromeLike.fill(QColor::fromRgbF(1.6f, 1.6f, 1.6f, 1.0f));

    for (int y = 8; y < 16; ++y) {
        auto* row = reinterpret_cast<float*>(chromeLike.scanLine(y));
        for (int x = 6; x < 18; ++x) {
            row[x * 4 + 0] = 0.22f;
            row[x * 4 + 1] = 0.22f;
            row[x * 4 + 2] = 0.22f;
            row[x * 4 + 3] = 1.0f;
        }
    }

    const QImage normalized = ais::capture::FrameNormalizer::normalizeToSdr(chromeLike);

    QVERIFY(normalized.colorSpace().isValid());
    QCOMPARE(normalized.colorSpace(), QColorSpace(QColorSpace::SRgb));

    const QColor bright = normalized.pixelColor(2, 2);
    const QColor dark = normalized.pixelColor(10, 10);

    QVERIFY2(bright.red() < 255,
             qPrintable(QStringLiteral("expected HDR white to be compressed, got %1").arg(bright.red())));
    QVERIFY2(bright.red() > 220,
             qPrintable(QStringLiteral("expected bright chrome background to stay bright, got %1").arg(bright.red())));
    QVERIFY2(dark.red() + 70 < bright.red(),
             qPrintable(QStringLiteral("expected contrast retention, bright=%1 dark=%2")
                            .arg(bright.red())
                            .arg(dark.red())));
}

void CapturePipelineTests::clippedSdrChromeLikeHighlightsAreCompressed() {
    QImage chromeLike(QSize(28, 28), QImage::Format_ARGB32_Premultiplied);
    chromeLike.setColorSpace(QColorSpace(QColorSpace::SRgb));
    chromeLike.fill(QColor(255, 255, 255));

    for (int y = 8; y < 20; ++y) {
        for (int x = 6; x < 22; ++x) {
            chromeLike.setPixelColor(x, y, QColor(70, 70, 70));
        }
    }

    const QImage normalized = ais::capture::FrameNormalizer::normalizeToSdr(chromeLike);

    QVERIFY(normalized.colorSpace().isValid());
    QCOMPARE(normalized.colorSpace(), QColorSpace(QColorSpace::SRgb));

    const QColor bright = normalized.pixelColor(2, 2);
    const QColor dark = normalized.pixelColor(10, 10);

    QVERIFY2(bright.red() < 250,
             qPrintable(QStringLiteral("expected clipped highlights to be compressed, got %1").arg(bright.red())));
    QVERIFY2(bright.red() > 210,
             qPrintable(QStringLiteral("expected bright region to stay bright, got %1").arg(bright.red())));
    QVERIFY2(dark.red() + 80 < bright.red(),
             qPrintable(QStringLiteral("expected preserved contrast, bright=%1 dark=%2")
                            .arg(bright.red())
                            .arg(dark.red())));
}

QTEST_MAIN(CapturePipelineTests)

#include "test_capture_pipeline.moc"
