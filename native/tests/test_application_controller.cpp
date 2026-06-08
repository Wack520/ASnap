#include <QClipboard>
#include <QGuiApplication>
#include <QPixmap>
#include <QtTest/QtTest>

#include "app/application_controller.h"
#include "capture/capture_selection.h"

using ais::app::ApplicationController;
using ais::app::BusyState;

class ApplicationControllerTests final : public QObject {
    Q_OBJECT

private slots:
    void captureAvailabilityRespectsBusyState();
    void plainCaptureCopiesScreenshotToClipboard();
    void settingsProviderTestRefreshesStatusAfterDialogClosed();
};

void ApplicationControllerTests::captureAvailabilityRespectsBusyState() {
    ApplicationController controller;

    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("Ready"));
    controller.ensureSettingsDialogForTest();

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(controller.canStartCaptureForTest());

    controller.forceBusyStateForTest(BusyState::RequestInFlight);
    QVERIFY(controller.canStartCaptureForTest());

    controller.forceBusyStateForTest(BusyState::TestingProvider);
    QVERIFY(!controller.canStartCaptureForTest());
}

void ApplicationControllerTests::plainCaptureCopiesScreenshotToClipboard() {
    ApplicationController controller;
    QPixmap image(48, 32);
    image.fill(Qt::red);

    controller.confirmCaptureForTest(ais::capture::CaptureSelection{
        .image = image,
        .localRect = QRect(0, 0, 48, 32),
        .virtualRect = QRect(0, 0, 48, 32),
    }, false);

    const QPixmap clipboardPixmap = QGuiApplication::clipboard()->pixmap();
    QVERIFY(!clipboardPixmap.isNull());
    QCOMPARE(clipboardPixmap.deviceIndependentSize().toSize(), QSize(48, 32));
}

void ApplicationControllerTests::settingsProviderTestRefreshesStatusAfterDialogClosed() {
    ApplicationController controller;

    controller.ensureSettingsDialogForTest();
    controller.forceBusyStateForTest(BusyState::TestingProvider);
    controller.closeSettingsDialogForTest();
    controller.forceBusyStateForTest(BusyState::Idle);
    controller.completeProviderTestForTest(false, true, QStringLiteral("OK"));

    QVERIFY(controller.canStartCaptureForTest());
    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("文字连接测试通过: OK"));
}

QTEST_MAIN(ApplicationControllerTests)

#include "test_application_controller.moc"
