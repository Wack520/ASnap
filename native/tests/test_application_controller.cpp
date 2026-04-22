#include <functional>

#include <QGuiApplication>
#include <QClipboard>
#include <QPixmap>
#include <QtTest/QtTest>

#include "ai/ai_client.h"
#include "app/application_controller.h"
#include "capture/capture_mode.h"
#include "capture/capture_selection.h"
#include "ui/settings/settingsdialog.h"

using ais::app::ApplicationController;
using ais::app::BusyState;

class ApplicationControllerTests final : public QObject {
    Q_OBJECT

private slots:
    void idleStateAllowsCapture();
    void requestBusyStateAllowsCaptureInterrupt();
    void ignoresCaptureShortcutWhileProviderTestIsBusy();
    void loadingConfigAppliesCaptureModeToCaptureService();
    void savingSettingsAppliesCaptureModeToCaptureService();
    void providerTestCompletionRefreshesStatusAfterSettingsDialogCloses();
    void provider429FailureUsesFriendlyStatus();
    void aiCaptureShowsPanelBeforeEncodingSelection();
    void aiCaptureDefersEncodingUntilNextEventLoopTurn();
    void closingChatPanelCancelsInFlightRequest();
    void closingChatPanelClearsQueuedFollowUpsAfterRequestFlowSplit();
    void queuedFollowUpAutoSendsAfterCurrentReplyCompletes();
    void emptyAssistantResponseAutomaticallyRetriesThreeTimes();
    void imageConversationEmptyResponseWaitsBeforeRetrying();
    void assetUploadFailureFallsBackToOpenAiCompatibleOnce();
    void reasoningOnlyAssistantResponseDoesNotRetry();
    void plainCaptureCopiesScreenshotToClipboard();
};

void ApplicationControllerTests::idleStateAllowsCapture() {
    ApplicationController controller;

    controller.forceBusyStateForTest(BusyState::Idle);

    QVERIFY(controller.canStartCaptureForTest());
}

void ApplicationControllerTests::requestBusyStateAllowsCaptureInterrupt() {
    ApplicationController controller;

    controller.forceBusyStateForTest(BusyState::RequestInFlight);

    QVERIFY(controller.canStartCaptureForTest());
}

void ApplicationControllerTests::ignoresCaptureShortcutWhileProviderTestIsBusy() {
    ApplicationController controller;

    controller.forceBusyStateForTest(BusyState::TestingProvider);

    QVERIFY(!controller.canStartCaptureForTest());
}

void ApplicationControllerTests::loadingConfigAppliesCaptureModeToCaptureService() {
    ApplicationController controller;
    ais::config::AppConfig config;
    config.captureMode = ais::capture::CaptureMode::HdrCompatible;

    controller.loadConfigForTest(config);

    QCOMPARE(static_cast<int>(controller.captureModeForTest()),
             static_cast<int>(ais::capture::CaptureMode::HdrCompatible));
}

void ApplicationControllerTests::savingSettingsAppliesCaptureModeToCaptureService() {
    ApplicationController controller;

    controller.loadConfigForTest(ais::config::AppConfig{});
    controller.ensureSettingsDialogForTest();

    ais::ui::SettingsDialog* const dialog = controller.settingsDialogForTest();
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->captureModeField() != nullptr);

    const int hdrCompatibleIndex =
        dialog->captureModeField()->findData(static_cast<int>(ais::capture::CaptureMode::HdrCompatible));
    QVERIFY(hdrCompatibleIndex >= 0);
    dialog->captureModeField()->setCurrentIndex(hdrCompatibleIndex);

    QVERIFY(QMetaObject::invokeMethod(&controller, "onSettingsSaveRequested", Qt::DirectConnection));
    QCOMPARE(static_cast<int>(controller.captureModeForTest()),
             static_cast<int>(ais::capture::CaptureMode::HdrCompatible));
}

void ApplicationControllerTests::providerTestCompletionRefreshesStatusAfterSettingsDialogCloses() {
    ApplicationController controller;

    controller.ensureSettingsDialogForTest();
    controller.forceBusyStateForTest(BusyState::TestingProvider);
    controller.closeSettingsDialogForTest();
    controller.forceBusyStateForTest(BusyState::Idle);
    controller.completeProviderTestForTest(false, true, QStringLiteral("OK"));

    QVERIFY(controller.canStartCaptureForTest());
    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("文字连接测试通过: OK"));
}

void ApplicationControllerTests::provider429FailureUsesFriendlyStatus() {
    ApplicationController controller;

    controller.completeProviderTestForTest(
        false,
        false,
        QStringLiteral("HTTP 429: {\"error\":{\"message\":\"Chat upstream returned429\",\"type\":\"upstream_error\",\"param\":\"\",\"code\":\"upstream_error\"}}"));

    QCOMPARE(controller.lastStatusTextForTest(),
             QStringLiteral("测试失败：服务当前限流（HTTP 429），请稍后再试，或更换模型 / 线路。"));
}

void ApplicationControllerTests::aiCaptureShowsPanelBeforeEncodingSelection() {
    ApplicationController controller;
    bool encoderSawVisiblePanel = false;

    controller.setImageEncoderForTest([&](const QPixmap& pixmap) {
        Q_UNUSED(pixmap);
        encoderSawVisiblePanel = controller.isChatPanelVisibleForTest();
        return QByteArray("encoded-image");
    });

    QPixmap image(320, 180);
    image.fill(Qt::blue);

    controller.confirmCaptureForTest(ais::capture::CaptureSelection{
        .image = image,
        .localRect = QRect(0, 0, 320, 180),
        .virtualRect = QRect(40, 50, 320, 180),
    }, true);

    QCoreApplication::processEvents();

    QVERIFY(encoderSawVisiblePanel);
}

void ApplicationControllerTests::aiCaptureDefersEncodingUntilNextEventLoopTurn() {
    ApplicationController controller;
    int encoderCallCount = 0;

    controller.setImageEncoderForTest([&](const QPixmap& pixmap) {
        Q_UNUSED(pixmap);
        encoderCallCount += 1;
        return QByteArray("encoded-image");
    });

    QPixmap image(320, 180);
    image.fill(Qt::darkCyan);

    controller.confirmCaptureForTest(ais::capture::CaptureSelection{
        .image = image,
        .localRect = QRect(0, 0, 320, 180),
        .virtualRect = QRect(40, 50, 320, 180),
    }, true);

    QCOMPARE(encoderCallCount, 0);

    QCoreApplication::processEvents();

    QCOMPARE(encoderCallCount, 1);
}

void ApplicationControllerTests::closingChatPanelCancelsInFlightRequest() {
    ApplicationController controller;

    controller.ensureChatPanelForTest();
    controller.forceBusyStateForTest(BusyState::RequestInFlight);
    controller.closeChatPanelForTest();

    QVERIFY(controller.canStartCaptureForTest());
    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("Ready"));
}

void ApplicationControllerTests::closingChatPanelClearsQueuedFollowUpsAfterRequestFlowSplit() {
    ApplicationController controller;

    controller.seedConversationForTest(QStringLiteral("Initial question"));
    controller.forceBusyStateForTest(BusyState::RequestInFlight);
    controller.followUpRequestedForTest(QStringLiteral("Queued follow-up"));
    QCOMPARE(controller.queuedFollowUpCountForTest(), 1);

    controller.closeChatPanelForTest();

    QCOMPARE(controller.queuedFollowUpCountForTest(), 0);
    QCOMPARE(controller.messageCountForTest(), 0);
    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("Ready"));
}

void ApplicationControllerTests::queuedFollowUpAutoSendsAfterCurrentReplyCompletes() {
    ApplicationController controller;
    int requestStartCount = 0;
    std::function<void(QString)> appendAssistantText;
    std::function<void()> completeCurrentRequest;

    controller.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile&,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler onTextDelta,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::CompletionHandler onComplete,
            ais::ai::AiClient::FailureHandler,
            int) {
            requestStartCount += 1;
            controller.forceBusyStateForTest(BusyState::RequestInFlight);
            appendAssistantText = std::move(onTextDelta);
            completeCurrentRequest = std::move(onComplete);
            return true;
        });
    controller.seedConversationForTest(QStringLiteral("Initial question"));

    controller.followUpRequestedForTest(QStringLiteral("First follow-up"));
    QCOMPARE(requestStartCount, 1);

    controller.followUpRequestedForTest(QStringLiteral("Queued follow-up"));
    QCOMPARE(controller.queuedFollowUpCountForTest(), 1);
    QCOMPARE(controller.queuedFollowUpTextForTest(0), QStringLiteral("Queued follow-up"));

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(appendAssistantText));
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    appendAssistantText(QStringLiteral("Current reply"));
    completeCurrentRequest();
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 2);
    QCOMPARE(controller.queuedFollowUpCountForTest(), 0);
    QCOMPARE(controller.lastUserMessageTextForTest(), QStringLiteral("Queued follow-up"));
}

void ApplicationControllerTests::emptyAssistantResponseAutomaticallyRetriesThreeTimes() {
    ApplicationController controller;
    int requestStartCount = 0;
    QList<int> retryAttempts;
    std::function<void()> completeCurrentRequest;
    controller.setEmptyRetryDelayOverrideForTest(0);

    controller.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile&,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::CompletionHandler onComplete,
            ais::ai::AiClient::FailureHandler,
            int retryAttempt) {
            requestStartCount += 1;
            retryAttempts.append(retryAttempt);
            controller.forceBusyStateForTest(BusyState::RequestInFlight);
            completeCurrentRequest = std::move(onComplete);
            return true;
        });
    controller.seedConversationForTest(QStringLiteral("Initial question"));

    controller.followUpRequestedForTest(QStringLiteral("Needs retry"));
    QCOMPARE(requestStartCount, 1);

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QTRY_COMPARE(requestStartCount, 2);

    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("AI 返回空内容，正在自动重试…"));

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QTRY_COMPARE(requestStartCount, 3);

    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("AI 返回空内容，正在自动重试…"));

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QTRY_COMPARE(requestStartCount, 4);

    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("AI 返回空内容，正在自动重试…"));

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 4);
    QCOMPARE(retryAttempts, QList<int>({0, 1, 2, 3}));
    QCOMPARE(controller.messageCountForTest(), 4);
    QCOMPARE(controller.lastAssistantMessageTextForTest(), QStringLiteral("(empty response)"));
}

void ApplicationControllerTests::imageConversationEmptyResponseWaitsBeforeRetrying() {
    ApplicationController controller;
    QCOMPARE(controller.emptyRetryDelayMsForTest(false, 0), 80);
    QCOMPARE(controller.emptyRetryDelayMsForTest(true, 0), 1200);
    QCOMPARE(controller.emptyRetryDelayMsForTest(true, 1), 2500);
    QCOMPARE(controller.emptyRetryDelayMsForTest(true, 2), 5000);
    QCOMPARE(controller.emptyRetryDelayMsForTest(true, 9), 5000);
}

void ApplicationControllerTests::assetUploadFailureFallsBackToOpenAiCompatibleOnce() {
    ApplicationController controller;
    int requestStartCount = 0;
    QList<ais::config::ProviderProtocol> protocols;
    std::function<void(QString)> failCurrentRequest;

    controller.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile& profile,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::CompletionHandler,
            ais::ai::AiClient::FailureHandler onFailure,
            int) {
            requestStartCount += 1;
            protocols.append(profile.protocol);
            controller.forceBusyStateForTest(BusyState::RequestInFlight);
            failCurrentRequest = std::move(onFailure);
            return true;
        });
    controller.seedConversationForTest(QStringLiteral("Initial question"));
    controller.followUpRequestedForTest(QStringLiteral("Analyze image"));
    QCOMPARE(requestStartCount, 1);

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(failCurrentRequest));
    failCurrentRequest(QStringLiteral("HTTP 400: {\"error\":{\"message\":\"Asset upload returned 400\",\"type\":\"upstream_error\",\"param\":\"\",\"code\":\"upstream_error\"}}"));
    QTRY_COMPARE(requestStartCount, 2);
    QCOMPARE(protocols.size(), 2);
    QCOMPARE(static_cast<int>(protocols.at(0)),
             static_cast<int>(ais::config::ProviderProtocol::OpenAiResponses));
    QCOMPARE(static_cast<int>(protocols.at(1)),
             static_cast<int>(ais::config::ProviderProtocol::OpenAiCompatible));

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(failCurrentRequest));
    failCurrentRequest(QStringLiteral("HTTP 400: {\"error\":{\"message\":\"Asset upload returned 400\",\"type\":\"upstream_error\",\"param\":\"\",\"code\":\"upstream_error\"}}"));
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 2);
    QVERIFY(controller.lastAssistantMessageTextForTest().contains(QStringLiteral("Asset upload returned 400")));
}

void ApplicationControllerTests::reasoningOnlyAssistantResponseDoesNotRetry() {
    ApplicationController controller;
    int requestStartCount = 0;
    std::function<void(QString)> appendAssistantReasoning;
    std::function<void()> completeCurrentRequest;

    controller.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile&,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::DeltaHandler onReasoningDelta,
            ais::ai::AiClient::CompletionHandler onComplete,
            ais::ai::AiClient::FailureHandler,
            int) {
            requestStartCount += 1;
            controller.forceBusyStateForTest(BusyState::RequestInFlight);
            appendAssistantReasoning = std::move(onReasoningDelta);
            completeCurrentRequest = std::move(onComplete);
            return true;
        });
    controller.seedConversationForTest(QStringLiteral("Initial question"));

    controller.followUpRequestedForTest(QStringLiteral("Reasoning only"));
    QCOMPARE(requestStartCount, 1);

    controller.forceBusyStateForTest(BusyState::Idle);
    QVERIFY(static_cast<bool>(appendAssistantReasoning));
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    appendAssistantReasoning(QStringLiteral("仅有思考，没有正文"));
    completeCurrentRequest();
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 1);
    QCOMPARE(controller.messageCountForTest(), 4);
    QCOMPARE(controller.lastAssistantMessageTextForTest(), QString());
    QCOMPARE(controller.lastAssistantReasoningForTest(), QStringLiteral("仅有思考，没有正文"));
    QCOMPARE(controller.lastStatusTextForTest(), QStringLiteral("Ready"));
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
    QCOMPARE(controller.messageCountForTest(), 0);
}

QTEST_MAIN(ApplicationControllerTests)

#include "test_application_controller.moc"
