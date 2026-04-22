#include <array>

#include <QMetaType>
#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSignalSpy>
#include <QTest>
#include <qfloat16.h>

#include "capture/capture_overlay.h"
#include "capture/capture_mode.h"
#include "capture/capture_selection.h"
#include "capture/desktop_capture_service.h"
#include "capture/desktop_snapshot.h"
#include "capture/screen_capture_backend.h"
#include "platform/windows/hotkey_chord.h"
#include "platform/windows/windows_graphics_capture_backend.h"

using ais::capture::CaptureOverlay;
using ais::capture::CaptureMode;
using ais::capture::CaptureSelection;
using ais::capture::DesktopCaptureService;
using ais::capture::DesktopSnapshot;
using ais::capture::ScreenCaptureBackend;
using ais::platform::windows::HotkeyChord;

namespace {

[[nodiscard]] DesktopSnapshot makeSnapshot() {
    QPixmap displayImage(160, 120);
    displayImage.fill(Qt::darkBlue);

    return DesktopSnapshot{
        .displayImage = displayImage,
        .captureImage = displayImage,
        .overlayGeometry = QRect(-120, 80, displayImage.width(), displayImage.height()),
        .virtualGeometry = QRect(-120, 80, displayImage.width(), displayImage.height()),
        .screenMappings = {ais::capture::ScreenMapping{
            .overlayRect = QRect(-120, 80, displayImage.width(), displayImage.height()),
            .virtualRect = QRect(-120, 80, displayImage.width(), displayImage.height()),
            .devicePixelRatio = 1.0,
        }},
    };
}

}  // namespace

class CaptureFlowTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void parsesAltQHotkeyChord();
    void translatesLocalSelectionToVirtualDesktopCoordinates();
    void logicalSelectionCopiesPhysicalPixelsForHighDpiSnapshots();
    void logicalSelectionDoesNotRenormalizePreparedSdrCapture();
    void translatesSelectionFromPhysicalOverlayToVirtualDesktopCoordinates();
    void snapshotForScreenKeepsPhysicalPixelsAndLogicalOverlayGeometry();
    void hdrLikeImageIsNormalizedToSdrColorSpace();
    void hdrLikeImageWithoutMetadataAssumesLinearSdrConversion();
    void chromeLikeHdrWhitesAreCompressedBeforeSdrClipping();
    void clippedSdrChromeLikeHighlightsAreCompressed();
    void composeFramesUseOverlayGeometryWithoutMixedDpiGap();
    void composeFramesSkipsEmptyRemoteFrames();
    void nativeFramesFallbackToDisplayOrderWhenNamesDoNotMatch();
    void bgraMappedTextureProducesArgb32Image();
    void halfFloatMappedTextureProducesLinearFp16Image();
    void shortMappedTextureRowPitchReturnsEmptyImage();
    void standardCaptureModePrefersWgcBackendsBeforeGdi();
    void hdrCompatibleCaptureModePrefersGdiBeforeWgcBackends();
    void overlayPaintUsesLogicalDisplayImageInsteadOfHighDpiCaptureImage();
    void overlayInitialRenderKeepsFrozenImageUndimmed();
    void overlayDraggingDimsOnlyOutsideSelection();
    void overlaySelectionUsesCornerHandlesInsteadOfFullOutline();
    void doubleClickWithoutSelectionCapturesWholeScreen();
    void doubleClickWithoutSelectionCapturesClickedScreenOnMultiMonitorDesktop();
    void dragReleaseEmitsConfirmedSelection();
    void escapeEmitsCancelled();
};

void CaptureFlowTests::initTestCase() {
    qRegisterMetaType<CaptureSelection>("ais::capture::CaptureSelection");
}

void CaptureFlowTests::parsesAltQHotkeyChord() {
    const auto chord = HotkeyChord::parse(QStringLiteral("Alt+Q"));

    QVERIFY(chord.has_value());
    QCOMPARE(chord->modifiers, Qt::AltModifier);
    QCOMPARE(chord->key, Qt::Key_Q);
}

void CaptureFlowTests::translatesLocalSelectionToVirtualDesktopCoordinates() {
    const QRect localRect(12, 16, 48, 24);
    const QPoint virtualOrigin(-120, 80);

    QCOMPARE(
        DesktopCaptureService::translateToVirtual(localRect, virtualOrigin),
        QRect(-108, 96, 48, 24));
}

void CaptureFlowTests::logicalSelectionCopiesPhysicalPixelsForHighDpiSnapshots() {
    QPixmap image(QSize(320, 240));
    image.fill(Qt::darkBlue);
    image.setDevicePixelRatio(2.0);

    const QRect logicalRect(10, 12, 40, 24);
    const QPixmap cropped = DesktopCaptureService::copyLogicalSelection(image, logicalRect);

    QCOMPARE(cropped.devicePixelRatio(), 2.0);
    QCOMPARE(cropped.deviceIndependentSize().toSize(), logicalRect.size());
    QCOMPARE(cropped.width(), logicalRect.width() * 2);
    QCOMPARE(cropped.height(), logicalRect.height() * 2);
}

void CaptureFlowTests::logicalSelectionDoesNotRenormalizePreparedSdrCapture() {
    QImage prepared(QSize(200, 120), QImage::Format_ARGB32_Premultiplied);
    prepared.setColorSpace(QColorSpace(QColorSpace::SRgb));
    prepared.fill(Qt::white);

    QPainter painter(&prepared);
    painter.fillRect(QRect(60, 30, 80, 60), QColor(48, 48, 48));
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(prepared);
    const QPixmap cropped = DesktopCaptureService::copyLogicalSelection(
        pixmap,
        QRect(0, 0, prepared.width(), prepared.height()));

    const QImage croppedImage = cropped.toImage();
    QCOMPARE(croppedImage.pixelColor(4, 4), prepared.pixelColor(4, 4));
    QCOMPARE(croppedImage.pixelColor(100, 60), prepared.pixelColor(100, 60));
}

void CaptureFlowTests::translatesSelectionFromPhysicalOverlayToVirtualDesktopCoordinates() {
    const DesktopSnapshot snapshot{
        .overlayGeometry = QRect(0, 0, 320, 100),
        .virtualGeometry = QRect(0, 0, 320, 100),
        .screenMappings = {
            ais::capture::ScreenMapping{
                .overlayRect = QRect(0, 0, 200, 100),
                .virtualRect = QRect(0, 0, 133, 67),
                .devicePixelRatio = 1.5,
            },
            ais::capture::ScreenMapping{
                .overlayRect = QRect(200, 0, 120, 80),
                .virtualRect = QRect(200, 0, 120, 80),
                .devicePixelRatio = 1.0,
            },
        },
    };

    QCOMPARE(DesktopCaptureService::translateToVirtual(snapshot, QRect(20, 10, 100, 40)),
             QRect(13, 7, 67, 27));
    QCOMPARE(DesktopCaptureService::translateToVirtual(snapshot, QRect(220, 10, 40, 30)),
             QRect(220, 10, 40, 30));
}

void CaptureFlowTests::snapshotForScreenKeepsPhysicalPixelsAndLogicalOverlayGeometry() {
    QPixmap displayImage(QSize(320, 100));
    displayImage.fill(Qt::black);
    QPainter painter(&displayImage);
    painter.fillRect(QRect(0, 0, 200, 100), Qt::red);
    painter.fillRect(QRect(200, 0, 120, 80), Qt::green);
    painter.end();

    const DesktopSnapshot snapshot{
        .displayImage = displayImage,
        .captureImage = displayImage,
        .overlayGeometry = QRect(0, 0, 320, 100),
        .virtualGeometry = QRect(0, 0, 320, 80),
        .screenMappings = {
            ais::capture::ScreenMapping{
                .overlayRect = QRect(0, 0, 200, 100),
                .virtualRect = QRect(0, 0, 133, 67),
                .devicePixelRatio = 1.5,
            },
            ais::capture::ScreenMapping{
                .overlayRect = QRect(200, 0, 120, 80),
                .virtualRect = QRect(200, 0, 120, 80),
                .devicePixelRatio = 1.0,
            },
        },
    };

    const DesktopSnapshot leftSnapshot =
        DesktopCaptureService::snapshotForScreen(snapshot, snapshot.screenMappings.constFirst());

    QCOMPARE(leftSnapshot.overlayGeometry, QRect(0, 0, 133, 67));
    QCOMPARE(leftSnapshot.virtualGeometry, QRect(0, 0, 133, 67));
    QCOMPARE(leftSnapshot.displayImage.width(), 200);
    QCOMPARE(leftSnapshot.displayImage.height(), 100);
    QCOMPARE(leftSnapshot.displayImage.devicePixelRatio(), 1.5);
    QCOMPARE(leftSnapshot.displayImage.deviceIndependentSize().toSize(), QSize(133, 67));
    QCOMPARE(leftSnapshot.screenMappings.size(), 1);
    QCOMPARE(leftSnapshot.screenMappings.constFirst().overlayRect, QRect(0, 0, 133, 67));
    QCOMPARE(leftSnapshot.screenMappings.constFirst().virtualRect, QRect(0, 0, 133, 67));
    QCOMPARE(leftSnapshot.screenMappings.constFirst().devicePixelRatio, 1.5);
}

void CaptureFlowTests::hdrLikeImageIsNormalizedToSdrColorSpace() {
    QImage hdrLike(QSize(32, 24), QImage::Format_RGBA16FPx4_Premultiplied);
    hdrLike.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    hdrLike.fill(Qt::white);

    const QImage normalized = DesktopCaptureService::normalizeForSdr(hdrLike);

    QVERIFY(normalized.colorSpace().isValid());
    QCOMPARE(normalized.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(normalized.format() == QImage::Format_ARGB32_Premultiplied ||
             normalized.format() == QImage::Format_RGBA8888_Premultiplied ||
             normalized.format() == QImage::Format_RGB32);
}

void CaptureFlowTests::hdrLikeImageWithoutMetadataAssumesLinearSdrConversion() {
    QImage hdrLike(QSize(16, 16), QImage::Format_RGBA16FPx4_Premultiplied);
    hdrLike.fill(QColor::fromRgbF(0.5f, 0.5f, 0.5f, 1.0f));

    const QImage normalized = DesktopCaptureService::normalizeForSdr(hdrLike);

    QVERIFY(normalized.colorSpace().isValid());
    QCOMPARE(normalized.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(normalized.pixelColor(4, 4).red() > 170);
}

void CaptureFlowTests::chromeLikeHdrWhitesAreCompressedBeforeSdrClipping() {
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

    const QImage normalized = DesktopCaptureService::normalizeForSdr(chromeLike);

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

void CaptureFlowTests::clippedSdrChromeLikeHighlightsAreCompressed() {
    QImage chromeLike(QSize(28, 28), QImage::Format_ARGB32_Premultiplied);
    chromeLike.setColorSpace(QColorSpace(QColorSpace::SRgb));
    chromeLike.fill(QColor(255, 255, 255));

    for (int y = 8; y < 20; ++y) {
        for (int x = 6; x < 22; ++x) {
            chromeLike.setPixelColor(x, y, QColor(70, 70, 70));
        }
    }

    const QImage normalized = DesktopCaptureService::normalizeForSdr(chromeLike);

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

void CaptureFlowTests::composeFramesUseOverlayGeometryWithoutMixedDpiGap() {
    QImage left(QSize(200, 100), QImage::Format_ARGB32_Premultiplied);
    left.fill(Qt::red);

    QImage right(QSize(120, 80), QImage::Format_ARGB32_Premultiplied);
    right.fill(Qt::green);

    const DesktopSnapshot snapshot = DesktopCaptureService::composeFrames({
        {
            .image = left,
            .overlayGeometry = QRect(0, 0, 200, 100),
            .virtualGeometry = QRect(0, 0, 133, 67),
            .devicePixelRatio = 1.5,
        },
        {
            .image = right,
            .overlayGeometry = QRect(200, 0, 120, 80),
            .virtualGeometry = QRect(200, 0, 120, 80),
            .devicePixelRatio = 1.0,
        },
    });

    QCOMPARE(snapshot.overlayGeometry, QRect(0, 0, 320, 100));
    QCOMPARE(snapshot.virtualGeometry, QRect(0, 0, 320, 80));
    QCOMPARE(snapshot.displayImage.deviceIndependentSize().toSize(), QSize(320, 100));

    const QImage rendered = snapshot.displayImage.toImage();
    QVERIFY(rendered.pixelColor(150, 40).red() > 200);
    QVERIFY(rendered.pixelColor(260, 40).green() > 150);
}

void CaptureFlowTests::composeFramesSkipsEmptyRemoteFrames() {
    QImage primary(QSize(160, 100), QImage::Format_ARGB32_Premultiplied);
    primary.fill(Qt::blue);

    const DesktopSnapshot snapshot = DesktopCaptureService::composeFrames({
        {
            .image = primary,
            .overlayGeometry = QRect(0, 0, 160, 100),
            .virtualGeometry = QRect(0, 0, 160, 100),
            .devicePixelRatio = 1.0,
        },
        {
            .image = QImage(),
            .overlayGeometry = QRect(160, 0, 160, 100),
            .virtualGeometry = QRect(160, 0, 160, 100),
            .devicePixelRatio = 2.0,
        },
    });

    QCOMPARE(snapshot.virtualGeometry, QRect(0, 0, 160, 100));
    QCOMPARE(snapshot.overlayGeometry, QRect(0, 0, 160, 100));
    QCOMPARE(snapshot.displayImage.deviceIndependentSize().toSize(), QSize(160, 100));
}

void CaptureFlowTests::nativeFramesFallbackToDisplayOrderWhenNamesDoNotMatch() {
    QImage left(QSize(300, 180), QImage::Format_ARGB32_Premultiplied);
    left.fill(Qt::red);
    QImage right(QSize(400, 240), QImage::Format_ARGB32_Premultiplied);
    right.fill(Qt::green);

    const QList<ais::platform::windows::NativeScreenFrame> nativeFrames{
        ais::platform::windows::NativeScreenFrame{
            .deviceName = QStringLiteral("UNRELATED-A"),
            .image = left,
            .monitorRect = QRect(0, 0, 300, 180),
        },
        ais::platform::windows::NativeScreenFrame{
            .deviceName = QStringLiteral("UNRELATED-B"),
            .image = right,
            .monitorRect = QRect(300, 0, 400, 240),
        },
    };

    const QList<ais::capture::detail::ScreenTarget> screenTargets{
        ais::capture::detail::ScreenTarget{
            .name = QStringLiteral("DISPLAY-LEFT"),
            .logicalGeometry = QRect(0, 0, 200, 120),
            .devicePixelRatio = 1.5,
        },
        ais::capture::detail::ScreenTarget{
            .name = QStringLiteral("DISPLAY-RIGHT"),
            .logicalGeometry = QRect(200, 0, 200, 120),
            .devicePixelRatio = 2.0,
        },
    };

    const QList<ais::capture::CapturedScreenFrame> frames =
        ais::capture::detail::mapNativeFramesToScreenTargets(nativeFrames, screenTargets);

    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).overlayGeometry, QRect(0, 0, 300, 180));
    QCOMPARE(frames.at(0).virtualGeometry, QRect(0, 0, 200, 120));
    QCOMPARE(frames.at(0).devicePixelRatio, 1.5);
    QCOMPARE(frames.at(1).overlayGeometry, QRect(300, 0, 400, 240));
    QCOMPARE(frames.at(1).virtualGeometry, QRect(200, 0, 200, 120));
    QCOMPARE(frames.at(1).devicePixelRatio, 2.0);
}

void CaptureFlowTests::bgraMappedTextureProducesArgb32Image() {
    const std::array<uchar, 8> rawPixels{
        0x10, 0x20, 0x30, 0xFF,
        0xAA, 0xBB, 0xCC, 0x80,
    };

    const QImage image = ais::platform::windows::detail::makeQImageFromMappedTexture(
        ais::platform::windows::detail::MappedTextureFormat::Bgra8Unorm,
        QSize(2, 1),
        8,
        rawPixels.data());

    QCOMPARE(image.format(), QImage::Format_ARGB32);
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(image.pixelColor(0, 0), QColor(0x30, 0x20, 0x10, 0xFF));
    QCOMPARE(image.pixelColor(1, 0), QColor(0xCC, 0xBB, 0xAA, 0x80));
}

void CaptureFlowTests::halfFloatMappedTextureProducesLinearFp16Image() {
    const std::array<qfloat16, 4> rawPixels{
        qfloat16(1.0f),
        qfloat16(0.5f),
        qfloat16(0.25f),
        qfloat16(1.0f),
    };

    const QImage image = ais::platform::windows::detail::makeQImageFromMappedTexture(
        ais::platform::windows::detail::MappedTextureFormat::Rgba16Float,
        QSize(1, 1),
        static_cast<qsizetype>(sizeof(rawPixels)),
        reinterpret_cast<const uchar*>(rawPixels.data()));

    QCOMPARE(image.format(), QImage::Format_RGBA16FPx4);
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));

    const auto* pixel = reinterpret_cast<const qfloat16*>(image.constScanLine(0));
    QCOMPARE(static_cast<float>(pixel[0]), 1.0f);
    QCOMPARE(static_cast<float>(pixel[1]), 0.5f);
    QCOMPARE(static_cast<float>(pixel[2]), 0.25f);
    QCOMPARE(static_cast<float>(pixel[3]), 1.0f);
}

void CaptureFlowTests::shortMappedTextureRowPitchReturnsEmptyImage() {
    const std::array<uchar, 3> rawPixels{0x10, 0x20, 0x30};

    const QImage image = ais::platform::windows::detail::makeQImageFromMappedTexture(
        ais::platform::windows::detail::MappedTextureFormat::Bgra8Unorm,
        QSize(2, 1),
        3,
        rawPixels.data());

    QVERIFY(image.isNull());
}

void CaptureFlowTests::standardCaptureModePrefersWgcBackendsBeforeGdi() {
    const QList<ScreenCaptureBackend> order =
        ais::platform::windows::backendOrderForCaptureMode(CaptureMode::Standard);

    QCOMPARE(order.size(), 3);
    QCOMPARE(order.at(0), ScreenCaptureBackend::WindowsGraphicsCaptureBgra);
    QCOMPARE(order.at(1), ScreenCaptureBackend::WindowsGraphicsCaptureFp16);
    QCOMPARE(order.at(2), ScreenCaptureBackend::Gdi);
}

void CaptureFlowTests::hdrCompatibleCaptureModePrefersGdiBeforeWgcBackends() {
    const QList<ScreenCaptureBackend> order =
        ais::platform::windows::backendOrderForCaptureMode(CaptureMode::HdrCompatible);

    QCOMPARE(order.size(), 3);
    QCOMPARE(order.at(0), ScreenCaptureBackend::Gdi);
    QCOMPARE(order.at(1), ScreenCaptureBackend::WindowsGraphicsCaptureBgra);
    QCOMPARE(order.at(2), ScreenCaptureBackend::WindowsGraphicsCaptureFp16);
}

void CaptureFlowTests::overlayPaintUsesLogicalDisplayImageInsteadOfHighDpiCaptureImage() {
    QPixmap displayImage(QSize(200, 120));
    displayImage.fill(Qt::blue);
    displayImage.setDevicePixelRatio(2.0);

    QPixmap captureImage(QSize(200, 120));
    captureImage.fill(Qt::red);
    captureImage.setDevicePixelRatio(2.0);

    CaptureOverlay overlay(DesktopSnapshot{
        .displayImage = displayImage,
        .captureImage = captureImage,
        .overlayGeometry = QRect(0, 0, displayImage.width(), displayImage.height()),
        .virtualGeometry = QRect(0, 0, displayImage.width(), displayImage.height()),
        .screenMappings = {ais::capture::ScreenMapping{
            .overlayRect = QRect(0, 0, displayImage.width(), displayImage.height()),
            .virtualRect = QRect(0, 0, displayImage.width(), displayImage.height()),
            .devicePixelRatio = 2.0,
        }},
    });

    overlay.show();
    QVERIFY(overlay.isVisible());

    QImage rendered(displayImage.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    overlay.render(&rendered);

    const QColor center = rendered.pixelColor(50, 30);
    QVERIFY2(center.blue() > center.red(),
             qPrintable(QStringLiteral("expected display image tint, got rgba(%1,%2,%3,%4)")
                            .arg(center.red())
                            .arg(center.green())
                            .arg(center.blue())
                            .arg(center.alpha())));
}

void CaptureFlowTests::overlayInitialRenderKeepsFrozenImageUndimmed() {
    CaptureOverlay overlay(makeSnapshot());
    overlay.show();
    QVERIFY(overlay.isVisible());

    QImage rendered(QSize(160, 120), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    overlay.render(&rendered);

    const QColor center = rendered.pixelColor(80, 60);
    QVERIFY2(center.blue() >= 120,
             qPrintable(QStringLiteral("expected undimmed frozen image, got rgba(%1,%2,%3,%4)")
                            .arg(center.red())
                            .arg(center.green())
                            .arg(center.blue())
                            .arg(center.alpha())));
}

void CaptureFlowTests::overlayDraggingDimsOnlyOutsideSelection() {
    CaptureOverlay overlay(makeSnapshot());
    overlay.show();
    QVERIFY(overlay.isVisible());

    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QTest::mouseMove(&overlay, QPoint(100, 80), 1);

    QImage rendered(QSize(160, 120), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    overlay.render(&rendered);

    const QColor outside = rendered.pixelColor(6, 6);
    const QColor inside = rendered.pixelColor(60, 50);
    QVERIFY2(outside.blue() < inside.blue(),
             qPrintable(QStringLiteral("expected outside area darker than selection, outside=%1 inside=%2")
                            .arg(outside.blue())
                            .arg(inside.blue())));
}

void CaptureFlowTests::overlaySelectionUsesCornerHandlesInsteadOfFullOutline() {
    CaptureOverlay overlay(makeSnapshot());
    overlay.show();
    QVERIFY(overlay.isVisible());

    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QTest::mouseMove(&overlay, QPoint(100, 80), 1);

    QImage rendered(QSize(160, 120), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    overlay.render(&rendered);

    const QColor topEdgeCenter = rendered.pixelColor(60, 20);
    const QColor topLeftCorner = rendered.pixelColor(20, 20);

    QVERIFY2(topLeftCorner.lightness() > topEdgeCenter.lightness() + 40,
             qPrintable(QStringLiteral("expected bright corner handle without full top border, corner=%1 edge=%2")
                            .arg(topLeftCorner.lightness())
                            .arg(topEdgeCenter.lightness())));
}

void CaptureFlowTests::doubleClickWithoutSelectionCapturesWholeScreen() {
    const DesktopSnapshot snapshot = makeSnapshot();
    CaptureOverlay overlay(snapshot);
    QSignalSpy confirmedSpy(&overlay, &CaptureOverlay::captureConfirmed);
    QSignalSpy cancelledSpy(&overlay, &CaptureOverlay::captureCancelled);

    overlay.show();
    QVERIFY(overlay.isVisible());

    QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));

    QTRY_COMPARE(confirmedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);

    const QList<QVariant> arguments = confirmedSpy.takeFirst();
    const CaptureSelection selection = arguments.constFirst().value<CaptureSelection>();
    const QRect expectedLocal = QRect(QPoint(0, 0), overlay.rect().size()).adjusted(0, 0, -1, -1);

    QCOMPARE(selection.localRect, expectedLocal);
    QCOMPARE(selection.image.deviceIndependentSize().toSize(), expectedLocal.size());
}

void CaptureFlowTests::doubleClickWithoutSelectionCapturesClickedScreenOnMultiMonitorDesktop() {
    QPixmap displayImage(QSize(448, 144));
    displayImage.fill(Qt::black);

    QPainter painter(&displayImage);
    painter.fillRect(QRect(0, 0, 54, 96), Qt::darkRed);
    painter.fillRect(QRect(192, 0, 256, 144), Qt::darkGreen);
    painter.end();

    const DesktopSnapshot snapshot{
        .displayImage = displayImage,
        .captureImage = displayImage,
        .overlayGeometry = QRect(0, 0, 448, 144),
        .virtualGeometry = QRect(0, 0, 448, 144),
        .screenMappings = {
            ais::capture::ScreenMapping{
                .overlayRect = QRect(0, 0, 54, 96),
                .virtualRect = QRect(0, 0, 54, 96),
                .devicePixelRatio = 1.0,
            },
            ais::capture::ScreenMapping{
                .overlayRect = QRect(192, 0, 256, 144),
                .virtualRect = QRect(192, 0, 256, 144),
                .devicePixelRatio = 1.0,
            },
        },
    };

    CaptureOverlay overlay(snapshot);
    QSignalSpy confirmedSpy(&overlay, &CaptureOverlay::captureConfirmed);
    QSignalSpy cancelledSpy(&overlay, &CaptureOverlay::captureCancelled);

    overlay.show();
    QVERIFY(overlay.isVisible());

    QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(260, 40));

    QTRY_COMPARE(confirmedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);

    const QList<QVariant> arguments = confirmedSpy.takeFirst();
    const CaptureSelection selection = arguments.constFirst().value<CaptureSelection>();
    const QRect expectedLocal = QRect(192, 0, 256, 144).adjusted(0, 0, -1, -1);

    QCOMPARE(selection.localRect, expectedLocal);
    QCOMPARE(selection.image.deviceIndependentSize().toSize(), expectedLocal.size());
}

void CaptureFlowTests::dragReleaseEmitsConfirmedSelection() {
    const DesktopSnapshot snapshot = makeSnapshot();
    CaptureOverlay overlay(snapshot);
    QSignalSpy confirmedSpy(&overlay, &CaptureOverlay::captureConfirmed);
    QSignalSpy cancelledSpy(&overlay, &CaptureOverlay::captureCancelled);

    overlay.show();
    QVERIFY(overlay.isVisible());

    const QPoint dragStart(14, 18);
    const QPoint dragEnd(74, 68);
    const QRect expectedLocal = QRect(dragStart, dragEnd).normalized();

    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(&overlay, dragEnd, 1);
    QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, dragEnd);

    QTRY_COMPARE(confirmedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);

    const QList<QVariant> arguments = confirmedSpy.takeFirst();
    const CaptureSelection selection = arguments.constFirst().value<CaptureSelection>();

    QCOMPARE(selection.localRect, expectedLocal);
    QCOMPARE(selection.virtualRect,
             DesktopCaptureService::translateToVirtual(
                 expectedLocal,
                 snapshot.virtualGeometry.topLeft()));
    QCOMPARE(selection.image.deviceIndependentSize().toSize(), expectedLocal.size());
}

void CaptureFlowTests::escapeEmitsCancelled() {
    CaptureOverlay overlay(makeSnapshot());
    QSignalSpy confirmedSpy(&overlay, &CaptureOverlay::captureConfirmed);
    QSignalSpy cancelledSpy(&overlay, &CaptureOverlay::captureCancelled);

    overlay.show();
    QVERIFY(overlay.isVisible());

    QTest::keyClick(&overlay, Qt::Key_Escape);

    QTRY_COMPARE(cancelledSpy.count(), 1);
    QCOMPARE(confirmedSpy.count(), 0);
}

QTEST_MAIN(CaptureFlowTests)

#include "test_capture_flow.moc"
