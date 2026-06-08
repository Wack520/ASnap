#include <QApplication>
#include <QPixmap>
#include <QtTest/QtTest>

#include "app/app_busy_state.h"
#include "app/capture_coordinator.h"
#include "app/request_guard.h"
#include "capture/capture_overlay.h"
#include "capture/capture_selection.h"
#include "capture/desktop_snapshot.h"

using ais::app::BusyState;
using ais::app::CaptureCoordinator;
using ais::capture::CaptureOverlay;
using ais::capture::CaptureSelection;
using ais::capture::DesktopSnapshot;

namespace {

struct HookRecorder {
    int aiSelectionCount = 0;
    int plainSelectionCount = 0;
    int cancelledCount = 0;
    CaptureSelection lastSelection{};
    bool hasSelection = false;
    QList<QString> statuses;
};

[[nodiscard]] DesktopSnapshot makeUsableSnapshot() {
    QPixmap image(96, 64);
    image.fill(Qt::darkBlue);

    return DesktopSnapshot{
        .displayImage = image,
        .captureImage = image,
        .overlayGeometry = QRect(0, 0, image.width(), image.height()),
        .virtualGeometry = QRect(0, 0, image.width(), image.height()),
        .screenMappings = {ais::capture::ScreenMapping{
            .overlayRect = QRect(0, 0, image.width(), image.height()),
            .virtualRect = QRect(0, 0, image.width(), image.height()),
        }},
    };
}

CaptureCoordinator::Hooks makeHooks(HookRecorder& recorder,
                                    std::function<DesktopSnapshot()> captureDesktop = {}) {
    CaptureCoordinator::Hooks hooks;
    hooks.captureDesktop = std::move(captureDesktop);
    hooks.onAiSelection = [&](const CaptureSelection& selection) {
        recorder.aiSelectionCount += 1;
        recorder.lastSelection = selection;
        recorder.hasSelection = true;
    };
    hooks.onPlainSelection = [&](const CaptureSelection& selection) {
        recorder.plainSelectionCount += 1;
        recorder.lastSelection = selection;
        recorder.hasSelection = true;
    };
    hooks.onCancelled = [&]() {
        recorder.cancelledCount += 1;
    };
    hooks.syncStatus = [&](const QString& status) {
        recorder.statuses.append(status);
    };
    return hooks;
}

[[nodiscard]] CaptureOverlay* findVisibleOverlay() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* overlay = qobject_cast<CaptureOverlay*>(widget);
        if (overlay != nullptr && overlay->isVisible()) {
            return overlay;
        }
    }

    return nullptr;
}

void closeAllCaptureOverlays() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* overlay = qobject_cast<CaptureOverlay*>(widget); overlay != nullptr) {
            overlay->close();
        }
    }
    QCoreApplication::processEvents();
}

}  // namespace

class CaptureCoordinatorTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void idleStateAllowsCapture();
    void requestInFlightStateInterruptsConversation();
    void providerTestBusyStateBlocksCapture();
    void startAiCaptureConfirmsSelectionAndSyncsStatus();
    void startPlainCaptureConfirmsSelectionAndSyncsStatus();
    void startCaptureFailsWhenCaptureHookMissing();
    void startCaptureFailsWhenSnapshotIsUnusable();
    void cancellingCaptureInvokesCallbackAndSyncsStatus();
};

void CaptureCoordinatorTests::init() {
    closeAllCaptureOverlays();
}

void CaptureCoordinatorTests::cleanup() {
    closeAllCaptureOverlays();
}

void CaptureCoordinatorTests::idleStateAllowsCapture() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder));
    ais::app::RequestGuard guard;

    QVERIFY(coordinator.canStartCaptureForState(guard.state()));
}

void CaptureCoordinatorTests::requestInFlightStateInterruptsConversation() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder));
    ais::app::RequestGuard guard;
    QVERIFY(guard.tryEnter(BusyState::RequestInFlight));

    QVERIFY(coordinator.shouldCancelConversationForState(guard.state()));
}

void CaptureCoordinatorTests::providerTestBusyStateBlocksCapture() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder));
    ais::app::RequestGuard guard;
    QVERIFY(guard.tryEnter(BusyState::TestingProvider));

    QVERIFY(!coordinator.canStartCaptureForState(guard.state()));
}

void CaptureCoordinatorTests::startAiCaptureConfirmsSelectionAndSyncsStatus() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder, []() {
        return makeUsableSnapshot();
    }));

    QVERIFY(coordinator.startAiCapture());
    QCOMPARE(recorder.statuses, QList<QString>{QStringLiteral("Select an area to capture...")});

    CaptureOverlay* overlay = nullptr;
    QTRY_VERIFY((overlay = findVisibleOverlay()) != nullptr);

    const QPoint dragStart(8, 10);
    const QPoint dragEnd(34, 28);
    const QRect expectedSelection = QRect(dragStart, dragEnd).normalized();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(overlay, dragEnd, 1);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, dragEnd);

    QTRY_COMPARE(recorder.aiSelectionCount, 1);
    QCOMPARE(recorder.plainSelectionCount, 0);
    QCOMPARE(recorder.cancelledCount, 0);
    QVERIFY(recorder.hasSelection);
    QCOMPARE(recorder.lastSelection.localRect, expectedSelection);
    QCOMPARE(recorder.lastSelection.image.deviceIndependentSize().toSize(), expectedSelection.size());
    QTRY_VERIFY(findVisibleOverlay() == nullptr);
}

void CaptureCoordinatorTests::startPlainCaptureConfirmsSelectionAndSyncsStatus() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder, []() {
        return makeUsableSnapshot();
    }));

    QVERIFY(coordinator.startPlainCapture());
    QCOMPARE(recorder.statuses, QList<QString>{QStringLiteral("选择截图区域…")});

    CaptureOverlay* overlay = nullptr;
    QTRY_VERIFY((overlay = findVisibleOverlay()) != nullptr);

    const QPoint dragStart(12, 14);
    const QPoint dragEnd(38, 36);
    const QRect expectedSelection = QRect(dragStart, dragEnd).normalized();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(overlay, dragEnd, 1);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, dragEnd);

    QTRY_COMPARE(recorder.plainSelectionCount, 1);
    QCOMPARE(recorder.aiSelectionCount, 0);
    QCOMPARE(recorder.cancelledCount, 0);
    QVERIFY(recorder.hasSelection);
    QCOMPARE(recorder.lastSelection.localRect, expectedSelection);
    QCOMPARE(recorder.lastSelection.image.deviceIndependentSize().toSize(), expectedSelection.size());
    QTRY_VERIFY(findVisibleOverlay() == nullptr);
}

void CaptureCoordinatorTests::startCaptureFailsWhenCaptureHookMissing() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder));

    QVERIFY(!coordinator.startAiCapture());
    QCOMPARE(recorder.aiSelectionCount, 0);
    QCOMPARE(recorder.plainSelectionCount, 0);
    QCOMPARE(recorder.cancelledCount, 0);
    const QList<QString> expectedStatuses{
        QStringLiteral("Select an area to capture..."),
        QStringLiteral("Capture service is unavailable"),
    };
    QCOMPARE(recorder.statuses, expectedStatuses);
    QVERIFY(findVisibleOverlay() == nullptr);
}

void CaptureCoordinatorTests::startCaptureFailsWhenSnapshotIsUnusable() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder, []() {
        return DesktopSnapshot{};
    }));

    QVERIFY(!coordinator.startPlainCapture());
    QCOMPARE(recorder.aiSelectionCount, 0);
    QCOMPARE(recorder.plainSelectionCount, 0);
    QCOMPARE(recorder.cancelledCount, 0);
    const QList<QString> expectedStatuses{
        QStringLiteral("选择截图区域…"),
        QStringLiteral("Failed to capture desktop"),
    };
    QCOMPARE(recorder.statuses, expectedStatuses);
    QVERIFY(findVisibleOverlay() == nullptr);
}

void CaptureCoordinatorTests::cancellingCaptureInvokesCallbackAndSyncsStatus() {
    HookRecorder recorder;
    CaptureCoordinator coordinator(makeHooks(recorder, []() {
        return makeUsableSnapshot();
    }));

    QVERIFY(coordinator.startAiCapture());
    CaptureOverlay* overlay = nullptr;
    QTRY_VERIFY((overlay = findVisibleOverlay()) != nullptr);

    QTest::keyClick(overlay, Qt::Key_Escape);

    QTRY_COMPARE(recorder.cancelledCount, 1);
    QCOMPARE(recorder.aiSelectionCount, 0);
    QCOMPARE(recorder.plainSelectionCount, 0);
    const QList<QString> expectedStatuses{
        QStringLiteral("Select an area to capture..."),
        QStringLiteral("Capture cancelled"),
    };
    QCOMPARE(recorder.statuses, expectedStatuses);
    QTRY_VERIFY(findVisibleOverlay() == nullptr);
}

QTEST_MAIN(CaptureCoordinatorTests)

#include "test_capture_coordinator.moc"
