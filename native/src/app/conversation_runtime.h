#pragma once

#include <functional>
#include <memory>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "ai/ai_client.h"
#include "app/app_busy_state.h"
#include "capture/capture_selection.h"
#include "config/app_config.h"

class QPixmap;

namespace ais::chat {
class ChatSession;
struct ChatMessage;
}  // namespace ais::chat

namespace ais::config {
struct ProviderProfile;
}  // namespace ais::config

namespace ais::app {

class ConversationRuntime final : public QObject {
public:
    using RequestStreamStarter = std::function<bool(const config::ProviderProfile&,
                                                    const QList<chat::ChatMessage>&,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::CompletionHandler,
                                                    ai::AiClient::FailureHandler,
                                                    int retryAttempt)>;

    struct Hooks {
        std::function<void(const QString&)> syncStatus;
        std::function<void()> refreshChatBinding;
        std::function<void(bool busy, const QString& status)> setChatBusy;
        std::function<void()> scheduleSessionRefresh;
        std::function<void()> ensureChatPanel;
        std::function<bool()> hasChatPanel;
        std::function<void()> showChatPanel;
        std::function<void()> raiseChatPanel;
        std::function<void()> activateChatPanel;
        std::function<void(const QRect&)> placeChatPanelNearSelection;
        std::function<BusyState()> busyState;
        std::function<bool()> isBusy;
        std::function<void()> cancelActiveRequest;
        std::function<QString()> statusForCurrentState;
        RequestStreamStarter requestStreamStarter;
    };

    explicit ConversationRuntime(Hooks hooks = {}, QObject* parent = nullptr);

    void setConfig(const config::AppConfig& config);
    void setRequestStreamStarterForTest(RequestStreamStarter starter);
    void setEmptyRetryDelayOverrideForTest(int delayMs);
    [[nodiscard]] int emptyRetryDelayMsForTest(bool hasImageContext, int emptyRetryAttempt) const;

    void seedConversationForTest(const QString& initialUserText);
    void followUpRequested(const QString& text);
    void beginSessionFromSelection(const capture::CaptureSelection& selection);
    [[nodiscard]] bool sendCurrentSessionRequest(
        const QString& busyStatus,
        int emptyRetryAttempt = 0,
        const config::ProviderProfile* requestProfileOverride = nullptr,
        bool allowAssetUploadFallback = true);
    void queueFollowUp(const QString& text);
    void scheduleQueuedFollowUpSend();
    void handleRequestCompleted(int emptyRetryAttempt);
    void cancelCurrentConversation(bool clearSession = true);

    [[nodiscard]] const std::shared_ptr<chat::ChatSession>& currentSession() const noexcept;
    [[nodiscard]] bool hasCurrentSession() const noexcept;
    [[nodiscard]] bool hasActiveSession() const noexcept { return hasCurrentSession(); }
    [[nodiscard]] int queuedFollowUpCountForTest() const noexcept;
    [[nodiscard]] QString queuedFollowUpTextForTest(int index) const;
    [[nodiscard]] int messageCountForTest() const;
    [[nodiscard]] QString lastUserMessageTextForTest() const;
    [[nodiscard]] QString lastAssistantMessageTextForTest() const;
    [[nodiscard]] QString lastAssistantReasoningForTest() const;
    [[nodiscard]] QString defaultFirstPrompt() const;
    [[nodiscard]] int emptyRetryDelayMs(bool hasImageContext, int emptyRetryAttempt) const;
    [[nodiscard]] QByteArray encodePng(const QPixmap& pixmap) const;

private:
    [[nodiscard]] BusyState currentBusyState() const;
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] bool hasChatPanel() const;
    [[nodiscard]] QString statusForCurrentState() const;
    void refreshChatBinding() const;
    void setChatBusy(bool busy, const QString& status) const;
    void syncStatus(const QString& status) const;
    [[nodiscard]] RequestStreamStarter requestStarter() const;

    Hooks hooks_;
    config::AppConfig config_;
    std::shared_ptr<chat::ChatSession> currentSession_;
    QStringList queuedFollowUpTexts_;
    RequestStreamStarter requestStreamStarter_;
    int emptyRetryDelayOverrideMs_ = -1;
};

}  // namespace ais::app
