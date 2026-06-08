#include <functional>

#include <QCoreApplication>
#include <QtTest/QtTest>

#include "ai/ai_client.h"
#include "app/app_busy_state.h"
#include "app/conversation_runtime.h"
#include "config/app_config.h"

using ais::app::BusyState;
using ais::app::ConversationRuntime;

namespace {

ConversationRuntime::Hooks makeHooks(BusyState& state,
                                     QString& statusText,
                                     int& cancelCount) {
    ConversationRuntime::Hooks hooks;
    hooks.syncStatus = [&](const QString& status) {
        statusText = status;
    };
    hooks.refreshChatBinding = []() {};
    hooks.setChatBusy = [](bool, const QString&) {};
    hooks.scheduleSessionRefresh = []() {};
    hooks.ensureChatPanel = []() {};
    hooks.hasChatPanel = []() { return true; };
    hooks.showChatPanel = []() {};
    hooks.raiseChatPanel = []() {};
    hooks.activateChatPanel = []() {};
    hooks.placeChatPanelNearSelection = [](const QRect&) {};
    hooks.busyState = [&state]() {
        return state;
    };
    hooks.isBusy = [&state]() {
        return state != BusyState::Idle;
    };
    hooks.cancelActiveRequest = [&]() {
        cancelCount += 1;
        state = BusyState::Idle;
    };
    hooks.statusForCurrentState = [&state]() {
        return state == BusyState::RequestInFlight
            ? QStringLiteral("Waiting for AI response...")
            : QStringLiteral("Ready");
    };
    return hooks;
}

}  // namespace

class ConversationRuntimeTests final : public QObject {
    Q_OBJECT

private slots:
    void queuedFollowUpAutoSendsAfterCurrentReplyCompletes();
    void emptyAssistantResponseAutomaticallyRetriesThreeTimes();
    void imageConversationEmptyResponseWaitsBeforeRetrying();
    void assetUploadFailureFallsBackToOpenAiCompatibleOnce();
    void reasoningOnlyAssistantResponseDoesNotRetry();
};

void ConversationRuntimeTests::queuedFollowUpAutoSendsAfterCurrentReplyCompletes() {
    BusyState state = BusyState::Idle;
    QString statusText;
    int cancelCount = 0;
    int requestStartCount = 0;
    std::function<void(QString)> appendAssistantText;
    std::function<void()> completeCurrentRequest;

    ConversationRuntime runtime(makeHooks(state, statusText, cancelCount));
    runtime.setConfig(ais::config::AppConfig{});
    runtime.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile&,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler onTextDelta,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::CompletionHandler onComplete,
            ais::ai::AiClient::FailureHandler,
            int) {
            requestStartCount += 1;
            state = BusyState::RequestInFlight;
            appendAssistantText = std::move(onTextDelta);
            completeCurrentRequest = std::move(onComplete);
            return true;
        });
    runtime.seedConversationForTest(QStringLiteral("Initial question"));

    runtime.followUpRequested(QStringLiteral("First follow-up"));
    QCOMPARE(requestStartCount, 1);

    runtime.followUpRequested(QStringLiteral("Queued follow-up"));
    QCOMPARE(runtime.queuedFollowUpCountForTest(), 1);
    QCOMPARE(runtime.queuedFollowUpTextForTest(0), QStringLiteral("Queued follow-up"));

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(appendAssistantText));
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    appendAssistantText(QStringLiteral("Current reply"));
    completeCurrentRequest();
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 2);
    QCOMPARE(runtime.queuedFollowUpCountForTest(), 0);
    QCOMPARE(runtime.lastUserMessageTextForTest(), QStringLiteral("Queued follow-up"));
}

void ConversationRuntimeTests::emptyAssistantResponseAutomaticallyRetriesThreeTimes() {
    BusyState state = BusyState::Idle;
    QString statusText;
    int cancelCount = 0;
    int requestStartCount = 0;
    QList<int> retryAttempts;
    std::function<void()> completeCurrentRequest;

    ConversationRuntime runtime(makeHooks(state, statusText, cancelCount));
    runtime.setConfig(ais::config::AppConfig{});
    runtime.setEmptyRetryDelayOverrideForTest(0);
    runtime.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile&,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::CompletionHandler onComplete,
            ais::ai::AiClient::FailureHandler,
            int retryAttempt) {
            requestStartCount += 1;
            retryAttempts.append(retryAttempt);
            state = BusyState::RequestInFlight;
            completeCurrentRequest = std::move(onComplete);
            return true;
        });
    runtime.seedConversationForTest(QStringLiteral("Initial question"));

    runtime.followUpRequested(QStringLiteral("Needs retry"));
    QCOMPARE(requestStartCount, 1);

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QTRY_COMPARE(requestStartCount, 2);

    QCOMPARE(statusText, QStringLiteral("AI 返回空内容，正在自动重试…"));

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QTRY_COMPARE(requestStartCount, 3);

    QCOMPARE(statusText, QStringLiteral("AI 返回空内容，正在自动重试…"));

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QTRY_COMPARE(requestStartCount, 4);

    QCOMPARE(statusText, QStringLiteral("AI 返回空内容，正在自动重试…"));

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    completeCurrentRequest();
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 4);
    QCOMPARE(retryAttempts, QList<int>({0, 1, 2, 3}));
    QCOMPARE(cancelCount, 3);
    QCOMPARE(runtime.messageCountForTest(), 4);
    QCOMPARE(runtime.lastAssistantMessageTextForTest(), QStringLiteral("(empty response)"));
}

void ConversationRuntimeTests::imageConversationEmptyResponseWaitsBeforeRetrying() {
    BusyState state = BusyState::Idle;
    QString statusText;
    int cancelCount = 0;
    ConversationRuntime runtime(makeHooks(state, statusText, cancelCount));

    QCOMPARE(runtime.emptyRetryDelayMsForTest(false, 0), 80);
    QCOMPARE(runtime.emptyRetryDelayMsForTest(true, 0), 1200);
    QCOMPARE(runtime.emptyRetryDelayMsForTest(true, 1), 2500);
    QCOMPARE(runtime.emptyRetryDelayMsForTest(true, 2), 5000);
    QCOMPARE(runtime.emptyRetryDelayMsForTest(true, 9), 5000);
}

void ConversationRuntimeTests::assetUploadFailureFallsBackToOpenAiCompatibleOnce() {
    BusyState state = BusyState::Idle;
    QString statusText;
    int cancelCount = 0;
    int requestStartCount = 0;
    QList<ais::config::ProviderProtocol> protocols;
    std::function<void(QString)> failCurrentRequest;

    ConversationRuntime runtime(makeHooks(state, statusText, cancelCount));
    runtime.setConfig(ais::config::AppConfig{});
    runtime.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile& profile,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::CompletionHandler,
            ais::ai::AiClient::FailureHandler onFailure,
            int) {
            requestStartCount += 1;
            protocols.append(profile.protocol);
            state = BusyState::RequestInFlight;
            failCurrentRequest = std::move(onFailure);
            return true;
        });
    runtime.seedConversationForTest(QStringLiteral("Initial question"));

    runtime.followUpRequested(QStringLiteral("Analyze image"));
    QCOMPARE(requestStartCount, 1);

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(failCurrentRequest));
    failCurrentRequest(QStringLiteral("HTTP 400: {\"error\":{\"message\":\"Asset upload returned 400\",\"type\":\"upstream_error\",\"param\":\"\",\"code\":\"upstream_error\"}}"));
    QTRY_COMPARE(requestStartCount, 2);
    QCOMPARE(protocols.size(), 2);
    QCOMPARE(static_cast<int>(protocols.at(0)),
             static_cast<int>(ais::config::ProviderProtocol::OpenAiResponses));
    QCOMPARE(static_cast<int>(protocols.at(1)),
             static_cast<int>(ais::config::ProviderProtocol::OpenAiCompatible));

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(failCurrentRequest));
    failCurrentRequest(QStringLiteral("HTTP 400: {\"error\":{\"message\":\"Asset upload returned 400\",\"type\":\"upstream_error\",\"param\":\"\",\"code\":\"upstream_error\"}}"));
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 2);
    QVERIFY(runtime.lastAssistantMessageTextForTest().contains(QStringLiteral("Asset upload returned 400")));
}

void ConversationRuntimeTests::reasoningOnlyAssistantResponseDoesNotRetry() {
    BusyState state = BusyState::Idle;
    QString statusText;
    int cancelCount = 0;
    int requestStartCount = 0;
    std::function<void(QString)> appendAssistantReasoning;
    std::function<void()> completeCurrentRequest;

    ConversationRuntime runtime(makeHooks(state, statusText, cancelCount));
    runtime.setConfig(ais::config::AppConfig{});
    runtime.setRequestStreamStarterForTest(
        [&](const ais::config::ProviderProfile&,
            const QList<ais::chat::ChatMessage>&,
            ais::ai::AiClient::DeltaHandler,
            ais::ai::AiClient::DeltaHandler onReasoningDelta,
            ais::ai::AiClient::CompletionHandler onComplete,
            ais::ai::AiClient::FailureHandler,
            int) {
            requestStartCount += 1;
            state = BusyState::RequestInFlight;
            appendAssistantReasoning = std::move(onReasoningDelta);
            completeCurrentRequest = std::move(onComplete);
            return true;
        });
    runtime.seedConversationForTest(QStringLiteral("Initial question"));

    runtime.followUpRequested(QStringLiteral("Reasoning only"));
    QCOMPARE(requestStartCount, 1);

    state = BusyState::Idle;
    QVERIFY(static_cast<bool>(appendAssistantReasoning));
    QVERIFY(static_cast<bool>(completeCurrentRequest));
    appendAssistantReasoning(QStringLiteral("仅有思考，没有正文"));
    completeCurrentRequest();
    QCoreApplication::processEvents();

    QCOMPARE(requestStartCount, 1);
    QCOMPARE(cancelCount, 0);
    QCOMPARE(runtime.messageCountForTest(), 4);
    QCOMPARE(runtime.lastAssistantMessageTextForTest(), QString());
    QCOMPARE(runtime.lastAssistantReasoningForTest(), QStringLiteral("仅有思考，没有正文"));
    QCOMPARE(statusText, QStringLiteral("Ready"));
}

QTEST_MAIN(ConversationRuntimeTests)

#include "test_conversation_runtime.moc"

