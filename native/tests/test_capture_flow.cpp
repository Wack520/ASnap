#include <array>
#include <optional>

#include <QMetaType>
#include <QColor>
#include <QColorSpace>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSignalSpy>
#include <QTest>
#include <qfloat16.h>

#include "capture/capture_overlay.h"
#include "capture/capture_pipeline_types.h"
#include "capture/capture_selection.h"
#include "capture/desktop_capture_service.h"
#include "capture/desktop_snapshot.h"
#include "capture/snapshot_composer.h"
#include "platform/windows/hotkey_chord.h"
#include "platform/windows/windows_capture_backend.h"
#include "platform/windows/windows_gdi_capture_backend.h"
#include "platform/windows/windows_graphics_capture_backend.h"

using ais::capture::CaptureOverlay;
using ais::capture::CaptureBackendKind;
using ais::capture::CaptureSelection;
using ais::capture::DesktopCaptureService;
using ais::capture::DesktopSnapshot;
using ais::capture::DisplayDescriptor;
using ais::capture::RawScreenFrame;
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
        }},
    };
}

struct BackendPlan {
    std::optional<RawScreenFrame> wgcFp16;
    QString wgcFp16FailureNote = QStringLiteral("WGC FP16 failed");
    std::optional<RawScreenFrame> wgcBgra;
    QString wgcBgraFailureNote = QStringLiteral("WGC BGRA failed");
    std::optional<RawScreenFrame> gdi;
    int gdiCalls = 0;
};

class WindowsCaptureBackendHarness {
public:
    WindowsCaptureBackendHarness() {
        hooks.captureWithWgc = &captureWithWgc;
        hooks.captureWithGdi = &captureWithGdi;
    }

    struct ScopedInstall final {
        explicit ScopedInstall(WindowsCaptureBackendHarness* harness)
            : previous(active) {
            active = harness;
            ais::platform::windows::detail::setWindowsCaptureBackendTestHooks(&harness->hooks);
        }

        ~ScopedInstall() {
            ais::platform::windows::detail::setWindowsCaptureBackendTestHooks(nullptr);
            active = previous;
        }

    private:
        WindowsCaptureBackendHarness* previous = nullptr;
    };

    static std::optional<RawScreenFrame> captureWithWgc(const DisplayDescriptor& display,
                                                        CaptureBackendKind preferredKind,
                                                        QString* failureNote) {
        if (active == nullptr) {
            return std::nullopt;
        }

        active->wgcRequests.append({display.deviceName, preferredKind});
        BackendPlan& plan = active->plans[display.deviceName];
        if (preferredKind == CaptureBackendKind::WgcFp16) {
            if (plan.wgcFp16.has_value()) {
                return plan.wgcFp16;
            }
            if (failureNote != nullptr) {
                *failureNote = plan.wgcFp16FailureNote;
            }
            return std::nullopt;
        }

        if (plan.wgcBgra.has_value()) {
            return plan.wgcBgra;
        }
        if (failureNote != nullptr) {
            *failureNote = plan.wgcBgraFailureNote;
        }
        return std::nullopt;
    }

    static std::optional<RawScreenFrame> captureWithGdi(const DisplayDescriptor& display) {
        if (active == nullptr) {
            return std::nullopt;
        }

        BackendPlan& plan = active->plans[display.deviceName];
        ++plan.gdiCalls;
        return plan.gdi;
    }

    QHash<QString, BackendPlan> plans;
    QList<QPair<QString, CaptureBackendKind>> wgcRequests;
    ais::platform::windows::detail::WindowsCaptureBackendTestHooks hooks;

private:
    static WindowsCaptureBackendHarness* active;
};

WindowsCaptureBackendHarness* WindowsCaptureBackendHarness::active = nullptr;

}  // namespace

class CaptureFlowTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void parsesAltQHotkeyChord();
    void bgraMappedTextureProducesArgb32Image();
    void halfFloatMappedTextureProducesLinearFp16Image();
    void shortMappedTextureRowPitchReturnsEmptyImage();
    void gdiRectConversionUsesExclusiveRightBottom();
    void wgcRectConversionUsesExclusiveRightBottom();
    void windowsCaptureBackendFallsBackToGdiWhenWgcFails();
    void windowsCaptureBackendCapturesEachDisplayOnceWithDiagnostics();
    void windowsCaptureBackendHdrCompatibleModePrefersGdiBeforeWgc();
    void overlayPaintUsesHighDpiCaptureImageWhenLogicalSizeMatches();
    void overlayPaintFallsBackToDisplayImageWhenCaptureWouldScale();
    void overlayUsesLogicalGeometryAndPhysicalSelectionOnSingleHighDpiScreen();
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

void CaptureFlowTests::gdiRectConversionUsesExclusiveRightBottom() {
    const RECT rect =
        ais::platform::windows::detail::makeWinRectForGdiCapture(QRect(10, 20, 30, 40));

    QCOMPARE(rect.left, 10L);
    QCOMPARE(rect.top, 20L);
    QCOMPARE(rect.right, 40L);
    QCOMPARE(rect.bottom, 60L);
}

void CaptureFlowTests::wgcRectConversionUsesExclusiveRightBottom() {
    const RECT rect = ais::platform::windows::detail::makeWinRectForWgcMonitorLookup(
        QRect(10, 20, 30, 40));

    QCOMPARE(rect.left, 10L);
    QCOMPARE(rect.top, 20L);
    QCOMPARE(rect.right, 40L);
    QCOMPARE(rect.bottom, 60L);
}

void CaptureFlowTests::windowsCaptureBackendFallsBackToGdiWhenWgcFails() {
    const DisplayDescriptor display{
        .deviceName = QStringLiteral("DISPLAY_A"),
        .monitorRect = QRect(0, 0, 40, 30),
        .virtualRect = QRect(0, 0, 40, 30),
        .devicePixelRatio = 1.0,
        .isPrimary = true,
    };

    QImage gdiImage(QSize(40, 30), QImage::Format_ARGB32_Premultiplied);
    gdiImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    gdiImage.fill(Qt::blue);

    WindowsCaptureBackendHarness harness;
    harness.plans.insert(display.deviceName,
                         BackendPlan{
                             .wgcFp16 = std::nullopt,
                             .wgcFp16FailureNote =
                                 QStringLiteral("WGC frame acquisition timed out"),
                             .wgcBgra = std::nullopt,
                             .wgcBgraFailureNote =
                                 QStringLiteral("WGC frame acquisition timed out"),
                             .gdi =
                                 RawScreenFrame{
                                     .display = display,
                                     .image = gdiImage,
                                     .backendKind = CaptureBackendKind::Gdi,
                                     .colorSpace = gdiImage.colorSpace(),
                                     .isHdrLike = false,
                                 },
                         });
    WindowsCaptureBackendHarness::ScopedInstall install(&harness);

    ais::platform::windows::WindowsScreenCaptureBackend backend;
    ais::capture::CaptureDiagnostics diagnostics;
    const QList<RawScreenFrame> frames =
        backend.captureDisplays({display}, &diagnostics, ais::capture::CaptureMode::Standard);
    const QList<QPair<QString, CaptureBackendKind>> expectedWgcRequests = {
        {display.deviceName, CaptureBackendKind::WgcBgra8},
        {display.deviceName, CaptureBackendKind::WgcFp16},
    };

    QCOMPARE(harness.wgcRequests, expectedWgcRequests);
    QCOMPARE(harness.plans.value(display.deviceName).gdiCalls, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.constFirst().backendKind, CaptureBackendKind::Gdi);
    QCOMPARE(diagnostics.entries.size(), 1);
    QCOMPARE(diagnostics.entries.constFirst().deviceName, display.deviceName);
    QCOMPARE(diagnostics.entries.constFirst().backendKind, CaptureBackendKind::Gdi);
    QVERIFY(diagnostics.entries.constFirst().fellBack);
    QCOMPARE(diagnostics.entries.constFirst().note,
             QStringLiteral("WGC frame acquisition timed out"));
}

void CaptureFlowTests::windowsCaptureBackendCapturesEachDisplayOnceWithDiagnostics() {
    const DisplayDescriptor firstDisplay{
        .deviceName = QStringLiteral("DISPLAY_1"),
        .monitorRect = QRect(0, 0, 40, 30),
        .virtualRect = QRect(0, 0, 40, 30),
        .devicePixelRatio = 1.0,
        .isPrimary = true,
    };
    const DisplayDescriptor secondDisplay{
        .deviceName = QStringLiteral("DISPLAY_2"),
        .monitorRect = QRect(40, 0, 30, 20),
        .virtualRect = QRect(40, 0, 30, 20),
        .devicePixelRatio = 1.0,
        .isPrimary = false,
    };

    QImage wgcImage(QSize(40, 30), QImage::Format_ARGB32_Premultiplied);
    wgcImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    wgcImage.fill(Qt::red);

    QImage gdiImage(QSize(30, 20), QImage::Format_ARGB32_Premultiplied);
    gdiImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    gdiImage.fill(Qt::green);

    WindowsCaptureBackendHarness harness;
    harness.plans.insert(firstDisplay.deviceName,
                         BackendPlan{
                             .wgcFp16 = std::nullopt,
                             .wgcFp16FailureNote = QStringLiteral("FP16 unavailable"),
                             .wgcBgra =
                                 RawScreenFrame{
                                     .display = firstDisplay,
                                     .image = wgcImage,
                                     .backendKind = CaptureBackendKind::WgcBgra8,
                                     .colorSpace = wgcImage.colorSpace(),
                                     .isHdrLike = false,
                                 },
                         });
    harness.plans.insert(secondDisplay.deviceName,
                         BackendPlan{
                             .wgcFp16 = std::nullopt,
                             .wgcFp16FailureNote =
                                 QStringLiteral("WGC frame acquisition timed out"),
                             .wgcBgra = std::nullopt,
                             .wgcBgraFailureNote =
                                 QStringLiteral("WGC frame acquisition timed out"),
                             .gdi =
                                 RawScreenFrame{
                                     .display = secondDisplay,
                                     .image = gdiImage,
                                     .backendKind = CaptureBackendKind::Gdi,
                                     .colorSpace = gdiImage.colorSpace(),
                                     .isHdrLike = false,
                                 },
                         });
    WindowsCaptureBackendHarness::ScopedInstall install(&harness);

    ais::platform::windows::WindowsScreenCaptureBackend backend;
    ais::capture::CaptureDiagnostics diagnostics;
    const QList<RawScreenFrame> frames = backend.captureDisplays({firstDisplay, secondDisplay},
                                                                 &diagnostics,
                                                                 ais::capture::CaptureMode::Standard);

    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).backendKind, CaptureBackendKind::WgcBgra8);
    QCOMPARE(frames.at(1).backendKind, CaptureBackendKind::Gdi);
    QCOMPARE(diagnostics.entries.size(), 2);
    QCOMPARE(diagnostics.entries.at(0).deviceName, firstDisplay.deviceName);
    QCOMPARE(diagnostics.entries.at(0).backendKind, CaptureBackendKind::WgcBgra8);
    QVERIFY(!diagnostics.entries.at(0).fellBack);
    QCOMPARE(diagnostics.entries.at(1).deviceName, secondDisplay.deviceName);
    QCOMPARE(diagnostics.entries.at(1).backendKind, CaptureBackendKind::Gdi);
    QCOMPARE(harness.plans.value(firstDisplay.deviceName).gdiCalls, 0);
    QCOMPARE(harness.plans.value(secondDisplay.deviceName).gdiCalls, 1);
}

void CaptureFlowTests::windowsCaptureBackendHdrCompatibleModePrefersGdiBeforeWgc() {
    const DisplayDescriptor display{
        .deviceName = QStringLiteral("DISPLAY_HDR"),
        .monitorRect = QRect(0, 0, 48, 32),
        .virtualRect = QRect(0, 0, 48, 32),
        .devicePixelRatio = 1.0,
        .isPrimary = true,
    };

    QImage gdiImage(QSize(48, 32), QImage::Format_ARGB32_Premultiplied);
    gdiImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    gdiImage.fill(Qt::yellow);

    QImage wgcImage(QSize(48, 32), QImage::Format_ARGB32_Premultiplied);
    wgcImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    wgcImage.fill(Qt::red);

    WindowsCaptureBackendHarness harness;
    harness.plans.insert(display.deviceName,
                         BackendPlan{
                             .wgcFp16 =
                                 RawScreenFrame{
                                     .display = display,
                                     .image = wgcImage,
                                     .backendKind = CaptureBackendKind::WgcFp16,
                                     .colorSpace = wgcImage.colorSpace(),
                                     .isHdrLike = true,
                                 },
                             .wgcBgra =
                                 RawScreenFrame{
                                     .display = display,
                                     .image = wgcImage,
                                     .backendKind = CaptureBackendKind::WgcBgra8,
                                     .colorSpace = wgcImage.colorSpace(),
                                     .isHdrLike = false,
                                 },
                             .gdi =
                                 RawScreenFrame{
                                     .display = display,
                                     .image = gdiImage,
                                     .backendKind = CaptureBackendKind::Gdi,
                                     .colorSpace = gdiImage.colorSpace(),
                                     .isHdrLike = false,
                                 },
                         });
    WindowsCaptureBackendHarness::ScopedInstall install(&harness);

    ais::platform::windows::WindowsScreenCaptureBackend backend;
    ais::capture::CaptureDiagnostics diagnostics;
    const QList<RawScreenFrame> frames =
        backend.captureDisplays({display},
                                &diagnostics,
                                ais::capture::CaptureMode::HdrCompatible);

    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.constFirst().backendKind, CaptureBackendKind::Gdi);
    QCOMPARE(harness.plans.value(display.deviceName).gdiCalls, 1);
    QVERIFY(harness.wgcRequests.isEmpty());
    QCOMPARE(diagnostics.entries.size(), 1);
    QCOMPARE(diagnostics.entries.constFirst().backendKind, CaptureBackendKind::Gdi);
}

void CaptureFlowTests::overlayPaintUsesHighDpiCaptureImageWhenLogicalSizeMatches() {
    QPixmap displayImage(QSize(200, 120));
    displayImage.fill(Qt::blue);
    displayImage.setDevicePixelRatio(2.0);

    QPixmap captureImage(QSize(200, 120));
    captureImage.fill(Qt::red);
    captureImage.setDevicePixelRatio(2.0);

    CaptureOverlay overlay(DesktopSnapshot{
        .displayImage = displayImage,
        .captureImage = captureImage,
        .overlayGeometry = QRect(0, 0, 100, 60),
        .virtualGeometry = QRect(0, 0, 100, 60),
        .captureGeometry = QRect(0, 0, 200, 120),
        .screenMappings = {ais::capture::ScreenMapping{
            .overlayRect = QRect(0, 0, 100, 60),
            .virtualRect = QRect(0, 0, 100, 60),
            .captureRect = QRect(0, 0, 200, 120),
            .captureDevicePixelRatio = 2.0,
        }},
    });

    overlay.show();
    QVERIFY(overlay.isVisible());
    QCOMPARE(overlay.size(), QSize(100, 60));

    QImage rendered(QSize(100, 60), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    overlay.render(&rendered);

    const QColor center = rendered.pixelColor(50, 30);
    QVERIFY2(center.red() > center.blue(),
             qPrintable(QStringLiteral("expected sharp capture image tint, got rgba(%1,%2,%3,%4)")
                            .arg(center.red())
                            .arg(center.green())
                            .arg(center.blue())
                            .arg(center.alpha())));
}

void CaptureFlowTests::overlayPaintFallsBackToDisplayImageWhenCaptureWouldScale() {
    QPixmap displayImage(QSize(100, 60));
    displayImage.fill(Qt::blue);

    QPixmap captureImage(QSize(200, 120));
    captureImage.fill(Qt::red);
    captureImage.setDevicePixelRatio(1.0);

    CaptureOverlay overlay(DesktopSnapshot{
        .displayImage = displayImage,
        .captureImage = captureImage,
        .overlayGeometry = QRect(0, 0, 100, 60),
        .virtualGeometry = QRect(0, 0, 100, 60),
        .captureGeometry = QRect(0, 0, 200, 120),
        .screenMappings = {ais::capture::ScreenMapping{
            .overlayRect = QRect(0, 0, 100, 60),
            .virtualRect = QRect(0, 0, 100, 60),
            .captureRect = QRect(0, 0, 200, 120),
            .captureDevicePixelRatio = 1.0,
        }},
    });

    overlay.show();
    QVERIFY(overlay.isVisible());
    QCOMPARE(overlay.size(), QSize(100, 60));

    QImage rendered(QSize(100, 60), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    overlay.render(&rendered);

    const QColor center = rendered.pixelColor(50, 30);
    QVERIFY2(center.blue() > center.red(),
             qPrintable(QStringLiteral("expected logical display image fallback, got rgba(%1,%2,%3,%4)")
                            .arg(center.red())
                            .arg(center.green())
                            .arg(center.blue())
                            .arg(center.alpha())));
}

void CaptureFlowTests::overlayUsesLogicalGeometryAndPhysicalSelectionOnSingleHighDpiScreen() {
    QImage image(QSize(300, 180), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::darkCyan);

    const DesktopSnapshot snapshot = ais::capture::SnapshotComposer::composeFrames({
        ais::capture::PreparedScreenFrame{
            .display = DisplayDescriptor{
                .deviceName = QStringLiteral("DISPLAY1"),
                .monitorRect = QRect(0, 0, 300, 180),
                .virtualRect = QRect(0, 0, 200, 120),
                .devicePixelRatio = 1.5,
                .isPrimary = true,
            },
            .normalizedImage = image,
            .backendKind = CaptureBackendKind::WgcFp16,
            .hdrToneMapped = true,
        },
    });
    const DesktopSnapshot perScreenSnapshot =
        ais::capture::SnapshotComposer::snapshotForScreen(snapshot, snapshot.screenMappings.constFirst());

    CaptureOverlay overlay(perScreenSnapshot);
    QSignalSpy confirmedSpy(&overlay, &CaptureOverlay::captureConfirmed);
    QSignalSpy cancelledSpy(&overlay, &CaptureOverlay::captureCancelled);

    overlay.show();
    QVERIFY(overlay.isVisible());
    QCOMPARE(overlay.size(), QSize(200, 120));

    const QPoint dragStart(20, 15);
    const QPoint dragEnd(60, 45);
    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(&overlay, dragEnd, 1);
    QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, dragEnd);

    QTRY_COMPARE(confirmedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);

    const CaptureSelection selection = confirmedSpy.takeFirst().constFirst().value<CaptureSelection>();
    QCOMPARE(selection.localRect, QRect(dragStart, dragEnd).normalized());
    QCOMPARE(selection.image.deviceIndependentSize().toSize(), QSize(41, 31));
    QCOMPARE(selection.image.devicePixelRatio(), 1.5);
    QCOMPARE(selection.image.width(), qRound(41 * 1.5));
    QCOMPARE(selection.image.height(), qRound(31 * 1.5));
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
            },
            ais::capture::ScreenMapping{
                .overlayRect = QRect(192, 0, 256, 144),
                .virtualRect = QRect(192, 0, 256, 144),
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
