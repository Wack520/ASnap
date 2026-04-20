#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QList>
#include <memory>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTest>

#include "capture/capture_pipeline_types.h"
#include "capture/desktop_capture_service.h"
#include "capture/desktop_snapshot.h"
#include "capture/display_topology.h"
#include "capture/frame_normalizer.h"
#include "capture/screen_capture_backend.h"
#include "capture/snapshot_composer.h"

namespace {

class FakeDisplayTopology final : public ais::capture::DisplayTopology {
public:
    [[nodiscard]] QList<ais::capture::DisplayDescriptor> enumerateDisplays() const override {
        return displays;
    }

    QList<ais::capture::DisplayDescriptor> displays;
};

class FakeScreenCaptureBackend final : public ais::capture::ScreenCaptureBackend {
public:
    [[nodiscard]] QList<ais::capture::RawScreenFrame> captureDisplays(
        const QList<ais::capture::DisplayDescriptor>& displays,
        ais::capture::CaptureDiagnostics* diagnostics) const override {
        requestedDisplays = displays;
        if (diagnostics != nullptr) {
            diagnostics->entries = diagnosticEntries;
        }
        return frames;
    }

    mutable QList<ais::capture::DisplayDescriptor> requestedDisplays;
    QList<ais::capture::RawScreenFrame> frames;
    QList<ais::capture::CaptureDiagnosticsEntry> diagnosticEntries;
};

}  // namespace

class CapturePipelineTests final : public QObject {
    Q_OBJECT

private slots:
    void capturePipelineTypesExposeStableDefaults();
    void hdrLikeImageIsNormalizedToSdrColorSpace();
    void hdrLikeImageWithoutMetadataAssumesLinearSdrConversion();
    void chromeLikeHdrWhitesAreCompressedBeforeSdrClipping();
    void clippedSdrChromeLikeHighlightsAreCompressed();
    void translatesLocalSelectionToVirtualDesktopCoordinates();
    void logicalSelectionCopiesPhysicalPixelsForHighDpiSnapshots();
    void translatesSelectionFromPhysicalOverlayToVirtualDesktopCoordinates();
    void snapshotForScreenKeepsPhysicalPixelsAndLogicalOverlayGeometry();
    void composeFramesUseOverlayGeometryWithoutMixedDpiGap();
    void composeFramesSkipsEmptyRemoteFrames();
    void desktopCaptureServiceUsesInjectedTopologyAndBackend();
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

void CapturePipelineTests::translatesLocalSelectionToVirtualDesktopCoordinates() {
    const QRect localRect(12, 16, 48, 24);
    const QPoint virtualOrigin(-120, 80);

    QCOMPARE(
        ais::capture::SnapshotComposer::translateToVirtual(localRect, virtualOrigin),
        QRect(-108, 96, 48, 24));
}

void CapturePipelineTests::logicalSelectionCopiesPhysicalPixelsForHighDpiSnapshots() {
    QPixmap image(QSize(320, 240));
    image.fill(Qt::darkBlue);
    image.setDevicePixelRatio(2.0);

    const QRect logicalRect(10, 12, 40, 24);
    const QPixmap cropped =
        ais::capture::SnapshotComposer::copyLogicalSelection(image, logicalRect);

    QCOMPARE(cropped.devicePixelRatio(), 2.0);
    QCOMPARE(cropped.deviceIndependentSize().toSize(), logicalRect.size());
    QCOMPARE(cropped.width(), logicalRect.width() * 2);
    QCOMPARE(cropped.height(), logicalRect.height() * 2);
}

void CapturePipelineTests::translatesSelectionFromPhysicalOverlayToVirtualDesktopCoordinates() {
    const ais::capture::DesktopSnapshot snapshot{
        .overlayGeometry = QRect(0, 0, 320, 100),
        .virtualGeometry = QRect(0, 0, 320, 100),
        .screenMappings = {
            ais::capture::ScreenMapping{
                .overlayRect = QRect(0, 0, 200, 100),
                .virtualRect = QRect(0, 0, 133, 67),
            },
            ais::capture::ScreenMapping{
                .overlayRect = QRect(200, 0, 120, 80),
                .virtualRect = QRect(200, 0, 120, 80),
            },
        },
    };

    QCOMPARE(ais::capture::SnapshotComposer::translateToVirtual(snapshot, QRect(20, 10, 100, 40)),
             QRect(13, 7, 67, 27));
    QCOMPARE(ais::capture::SnapshotComposer::translateToVirtual(snapshot, QRect(220, 10, 40, 30)),
             QRect(220, 10, 40, 30));
}

void CapturePipelineTests::snapshotForScreenKeepsPhysicalPixelsAndLogicalOverlayGeometry() {
    QPixmap displayImage(QSize(300, 120));
    displayImage.fill(Qt::darkBlue);

    QPixmap captureImage(QSize(600, 240));
    captureImage.fill(Qt::darkGreen);
    captureImage.setDevicePixelRatio(2.0);

    const ais::capture::ScreenMapping screenMapping{
        .overlayRect = QRect(280, 50, 120, 90),
        .virtualRect = QRect(280, 50, 120, 90),
    };
    const ais::capture::DesktopSnapshot snapshot{
        .displayImage = displayImage,
        .captureImage = captureImage,
        .overlayGeometry = QRect(100, 50, 300, 120),
        .virtualGeometry = QRect(100, 50, 300, 120),
        .screenMappings = {
            ais::capture::ScreenMapping{
                .overlayRect = QRect(100, 50, 180, 120),
                .virtualRect = QRect(100, 50, 180, 120),
            },
            screenMapping,
        },
    };

    const ais::capture::DesktopSnapshot screenSnapshot =
        ais::capture::SnapshotComposer::snapshotForScreen(snapshot, screenMapping);

    QCOMPARE(screenSnapshot.overlayGeometry, screenMapping.overlayRect);
    QCOMPARE(screenSnapshot.virtualGeometry, screenMapping.virtualRect);
    QCOMPARE(screenSnapshot.screenMappings.size(), 1);
    QCOMPARE(screenSnapshot.screenMappings.constFirst().overlayRect, screenMapping.overlayRect);
    QCOMPARE(screenSnapshot.displayImage.deviceIndependentSize().toSize(), screenMapping.overlayRect.size());
    QCOMPARE(screenSnapshot.captureImage.deviceIndependentSize().toSize(), screenMapping.overlayRect.size());
    QCOMPARE(screenSnapshot.captureImage.width(), screenMapping.overlayRect.width() * 2);
    QCOMPARE(screenSnapshot.captureImage.height(), screenMapping.overlayRect.height() * 2);
}

void CapturePipelineTests::composeFramesUseOverlayGeometryWithoutMixedDpiGap() {
    QImage left(QSize(200, 100), QImage::Format_ARGB32_Premultiplied);
    left.fill(Qt::red);

    QImage right(QSize(120, 80), QImage::Format_ARGB32_Premultiplied);
    right.fill(Qt::green);

    const QList<ais::capture::PreparedScreenFrame> frames{
        {
            .display = ais::capture::DisplayDescriptor{
                .deviceName = QStringLiteral("DISPLAY1"),
                .monitorRect = QRect(0, 0, 200, 100),
                .virtualRect = QRect(0, 0, 133, 67),
                .devicePixelRatio = 1.5,
                .isPrimary = true,
            },
            .normalizedImage = left,
            .backendKind = ais::capture::CaptureBackendKind::WgcFp16,
            .hdrToneMapped = true,
        },
        {
            .display = ais::capture::DisplayDescriptor{
                .deviceName = QStringLiteral("DISPLAY2"),
                .monitorRect = QRect(200, 0, 120, 80),
                .virtualRect = QRect(200, 0, 120, 80),
                .devicePixelRatio = 1.0,
                .isPrimary = false,
            },
            .normalizedImage = right,
            .backendKind = ais::capture::CaptureBackendKind::WgcBgra8,
            .hdrToneMapped = false,
        },
    };
    const ais::capture::CaptureDiagnostics diagnostics{
        .entries = {
            ais::capture::CaptureDiagnosticsEntry{
                .deviceName = QStringLiteral("DISPLAY1"),
                .backendKind = ais::capture::CaptureBackendKind::WgcFp16,
                .hdrToneMapped = true,
                .fellBack = false,
                .note = QStringLiteral("tone mapped"),
            },
            ais::capture::CaptureDiagnosticsEntry{
                .deviceName = QStringLiteral("DISPLAY2"),
                .backendKind = ais::capture::CaptureBackendKind::WgcBgra8,
                .hdrToneMapped = false,
                .fellBack = false,
                .note = QStringLiteral("native"),
            },
        },
    };

    const ais::capture::DesktopSnapshot snapshot =
        ais::capture::SnapshotComposer::composeFrames(frames, diagnostics);

    QCOMPARE(snapshot.overlayGeometry, QRect(0, 0, 320, 100));
    QCOMPARE(snapshot.virtualGeometry, QRect(0, 0, 320, 80));
    QCOMPARE(snapshot.displayImage.deviceIndependentSize().toSize(), QSize(320, 100));
    QCOMPARE(snapshot.diagnostics.entries.size(), 2);
    QCOMPARE(snapshot.diagnostics.entries.constFirst().deviceName, QStringLiteral("DISPLAY1"));

    const QImage rendered = snapshot.displayImage.toImage();
    QVERIFY(rendered.pixelColor(150, 40).red() > 200);
    QVERIFY(rendered.pixelColor(260, 40).green() > 150);
}

void CapturePipelineTests::composeFramesSkipsEmptyRemoteFrames() {
    QImage primary(QSize(160, 100), QImage::Format_ARGB32_Premultiplied);
    primary.fill(Qt::blue);

    const ais::capture::DesktopSnapshot snapshot = ais::capture::SnapshotComposer::composeFrames({
        ais::capture::PreparedScreenFrame{
            .display = ais::capture::DisplayDescriptor{
                .deviceName = QStringLiteral("DISPLAY1"),
                .monitorRect = QRect(0, 0, 160, 100),
                .virtualRect = QRect(0, 0, 160, 100),
                .devicePixelRatio = 1.0,
                .isPrimary = true,
            },
            .normalizedImage = primary,
            .backendKind = ais::capture::CaptureBackendKind::Gdi,
            .hdrToneMapped = false,
        },
        ais::capture::PreparedScreenFrame{
            .display = ais::capture::DisplayDescriptor{
                .deviceName = QStringLiteral("DISPLAY2"),
                .monitorRect = QRect(160, 0, 160, 100),
                .virtualRect = QRect(160, 0, 160, 100),
                .devicePixelRatio = 2.0,
                .isPrimary = false,
            },
            .normalizedImage = QImage(),
            .backendKind = ais::capture::CaptureBackendKind::Unknown,
            .hdrToneMapped = false,
        },
    });

    QCOMPARE(snapshot.virtualGeometry, QRect(0, 0, 160, 100));
    QCOMPARE(snapshot.overlayGeometry, QRect(0, 0, 160, 100));
    QCOMPARE(snapshot.displayImage.deviceIndependentSize().toSize(), QSize(160, 100));
}

void CapturePipelineTests::desktopCaptureServiceUsesInjectedTopologyAndBackend() {
    using namespace ais::capture;

    auto fakeTopology = std::make_unique<FakeDisplayTopology>();
    fakeTopology->displays = {
        DisplayDescriptor{
            .deviceName = QStringLiteral("FAKE_DISPLAY_1"),
            .monitorRect = QRect(0, 0, 60, 40),
            .virtualRect = QRect(0, 0, 60, 40),
            .devicePixelRatio = 1.0,
            .isPrimary = true,
        },
        DisplayDescriptor{
            .deviceName = QStringLiteral("FAKE_DISPLAY_2"),
            .monitorRect = QRect(60, 0, 50, 40),
            .virtualRect = QRect(60, 0, 50, 40),
            .devicePixelRatio = 1.0,
            .isPrimary = false,
        },
    };
    FakeDisplayTopology* topology = fakeTopology.get();

    auto fakeBackend = std::make_unique<FakeScreenCaptureBackend>();
    QImage left(QSize(60, 40), QImage::Format_ARGB32_Premultiplied);
    left.setColorSpace(QColorSpace(QColorSpace::SRgb));
    left.fill(Qt::red);

    QImage right(QSize(50, 40), QImage::Format_ARGB32_Premultiplied);
    right.setColorSpace(QColorSpace(QColorSpace::SRgb));
    right.fill(Qt::green);

    fakeBackend->frames = {
        RawScreenFrame{
            .display = topology->displays.at(0),
            .image = left,
            .backendKind = CaptureBackendKind::Gdi,
            .colorSpace = QColorSpace(QColorSpace::SRgb),
            .isHdrLike = false,
        },
        RawScreenFrame{
            .display = topology->displays.at(1),
            .image = right,
            .backendKind = CaptureBackendKind::WgcBgra8,
            .colorSpace = QColorSpace(QColorSpace::SRgb),
            .isHdrLike = false,
        },
    };
    fakeBackend->diagnosticEntries = {
        CaptureDiagnosticsEntry{
            .deviceName = QStringLiteral("FAKE_DISPLAY_1"),
            .backendKind = CaptureBackendKind::Gdi,
            .hdrToneMapped = false,
            .fellBack = false,
            .note = QStringLiteral("fake-left"),
        },
        CaptureDiagnosticsEntry{
            .deviceName = QStringLiteral("FAKE_DISPLAY_2"),
            .backendKind = CaptureBackendKind::WgcBgra8,
            .hdrToneMapped = false,
            .fellBack = false,
            .note = QStringLiteral("fake-right"),
        },
    };
    FakeScreenCaptureBackend* backend = fakeBackend.get();

    DesktopCaptureService service(std::move(fakeTopology), std::move(fakeBackend));

    const DesktopSnapshot snapshot = service.captureVirtualDesktop();

    QCOMPARE(backend->requestedDisplays.size(), topology->displays.size());
    for (int index = 0; index < topology->displays.size(); ++index) {
        const DisplayDescriptor& requested = backend->requestedDisplays.at(index);
        const DisplayDescriptor& expected = topology->displays.at(index);
        QCOMPARE(requested.deviceName, expected.deviceName);
        QCOMPARE(requested.monitorRect, expected.monitorRect);
        QCOMPARE(requested.virtualRect, expected.virtualRect);
        QCOMPARE(requested.devicePixelRatio, expected.devicePixelRatio);
        QCOMPARE(requested.isPrimary, expected.isPrimary);
    }

    QCOMPARE(snapshot.overlayGeometry, QRect(0, 0, 110, 40));
    QCOMPARE(snapshot.virtualGeometry, QRect(0, 0, 110, 40));
    QCOMPARE(snapshot.screenMappings.size(), 2);
    QCOMPARE(snapshot.screenMappings.at(0).overlayRect, topology->displays.at(0).monitorRect);
    QCOMPARE(snapshot.screenMappings.at(1).overlayRect, topology->displays.at(1).monitorRect);
    QCOMPARE(snapshot.diagnostics.entries.size(), 2);
    QCOMPARE(snapshot.diagnostics.entries.constFirst().deviceName, QStringLiteral("FAKE_DISPLAY_1"));

    const QImage rendered = snapshot.displayImage.toImage();
    QVERIFY(rendered.pixelColor(20, 20).red() > 200);
    QVERIFY(rendered.pixelColor(80, 20).green() > 150);
}

QTEST_MAIN(CapturePipelineTests)

#include "test_capture_pipeline.moc"
