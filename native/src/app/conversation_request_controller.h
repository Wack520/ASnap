#pragma once

#include <functional>
#include <memory>

#include <QByteArray>
#include <QObject>
#include <QStringList>

#include "ai/ai_client.h"
#include "app/request_guard.h"
#include "config/provider_profile.h"

namespace ais::chat {
struct ChatMessage;
class ChatSession;
}  // namespace ais::chat

namespace ais::app {

class ConversationRequestController final : public QObject {
    Q_OBJECT

public:
    using RequestStreamStarter = std::function<bool(const config::ProviderProfile&,
                                                    const QList<chat::ChatMessage>&,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::CompletionHandler,
                                                    ai::AiClient::FailureHandler,
                                                    int retryAttempt)>;
    using ActiveProfileProvider = std::function<config::ProviderProfile()>;
    using RequestCancelHandler = std::function<void()>;
    using SessionBindHandler = std::function<void(const std::shared_ptr<chat::ChatSession>&)>;
    using SessionRefreshHandler = std::function<void()>;
    using PanelBusyHandler = std::function<void(bool busy, const QString& status)>;
    using StatusSyncHandler = std::function<void(const QString& status)>;
    using StatusForStateHandler = std::function<QString(BusyState)>;

    struct Hooks {
        ActiveProfileProvider activeProfileProvider;
        RequestStreamStarter requestStreamStarter;
        RequestCancelHandler cancelActiveRequest;
        SessionBindHandler bindSession;
        SessionRefreshHandler scheduleSessionRefresh;
        PanelBusyHandler setPanelBusy;
        StatusSyncHandler syncStatus;
        StatusForStateHandler statusForState;
    };

    explicit ConversationRequestController(RequestGuard& guard,
                                           Hooks hooks,
                                           QObject* parent = nullptr);

    void beginSession(QByteArray initialImage,
                      const QString& initialUserText);
    void beginTextSession(const QString& initialUserText);
    [[nodiscard]] bool hasSession() const noexcept { return currentSession_ != nullptr; }
    [[nodiscard]] const std::shared_ptr<chat::ChatSession>& session() const noexcept { return currentSession_; }

    void onFollowUpRequested(const QString& text);
    [[nodiscard]] bool startCurrentSessionRequest(
        const QString& busyStatus,
        int emptyRetryAttempt = 0,
        const config::ProviderProfile* requestProfileOverride = nullptr,
        bool allowAssetUploadFallback = true);
    void cancelCurrentConversation(bool clearSession = true);

    void setRequestStreamStarterForTest(RequestStreamStarter starter);
    void setEmptyRetryDelayOverrideForTest(int delayMs);
    [[nodiscard]] int emptyRetryDelayMsForTest(bool hasImageContext, int emptyRetryAttempt) const;
    [[nodiscard]] int queuedFollowUpCountForTest() const noexcept { return queuedFollowUpTexts_.size(); }
    [[nodiscard]] QString queuedFollowUpTextForTest(int index) const;
    [[nodiscard]] int messageCountForTest() const;
    [[nodiscard]] QString lastUserMessageTextForTest() const;
    [[nodiscard]] QString lastAssistantMessageTextForTest() const;
    [[nodiscard]] QString lastAssistantReasoningForTest() const;

private:
    [[nodiscard]] bool sendCurrentSessionRequest(
        const QString& busyStatus,
        int emptyRetryAttempt,
        const config::ProviderProfile* requestProfileOverride,
        bool allowAssetUploadFallback);
    void queueFollowUp(const QString& text);
    void scheduleQueuedFollowUpSend();
    void handleRequestCompleted(int emptyRetryAttempt);

    [[nodiscard]] int emptyRetryDelayMs(bool hasImageContext, int emptyRetryAttempt) const;
    [[nodiscard]] QString statusForState(BusyState state) const;
    void bindSession() const;
    void scheduleSessionRefresh() const;
    void setPanelBusy(bool busy, const QString& status) const;
    void syncStatus(const QString& status) const;
    void cancelActiveRequest();

    RequestGuard& guard_;
    Hooks hooks_;
    std::shared_ptr<chat::ChatSession> currentSession_;
    QStringList queuedFollowUpTexts_;
    RequestStreamStarter requestStreamStarter_;
    int emptyRetryDelayOverrideMs_ = -1;
};

}  // namespace ais::app
