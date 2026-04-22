#include "app/conversation_request_controller.h"

#include <utility>

#include <QTimer>

#include "chat/chat_session.h"
#include "config/provider_preset.h"

namespace ais::app {

namespace {

constexpr int kMaxEmptyResponseRetries = 3;

[[nodiscard]] config::ProviderProfile withDefaults(config::ProviderProfile profile) {
    const auto preset = config::presetFor(profile.protocol);
    if (profile.baseUrl.trimmed().isEmpty()) {
        profile.baseUrl = preset.defaultBaseUrl;
    }
    if (profile.model.trimmed().isEmpty()) {
        profile.model = preset.defaultModel;
    }
    return profile;
}

[[nodiscard]] bool messagesContainImage(const QList<chat::ChatMessage>& messages) {
    for (const chat::ChatMessage& message : messages) {
        if (message.hasImage()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int defaultEmptyRetryDelayMs(const bool hasImageContext,
                                           const int emptyRetryAttempt) {
    if (!hasImageContext) {
        return 80;
    }

    switch (qMax(0, emptyRetryAttempt)) {
    case 0:
        return 1200;
    case 1:
        return 2500;
    default:
        return 5000;
    }
}

[[nodiscard]] bool isAssetUploadFailure(const QString& error) {
    return error.contains(QStringLiteral("Asset upload returned 400"), Qt::CaseInsensitive) ||
           error.contains(QStringLiteral("asset upload"), Qt::CaseInsensitive);
}

}  // namespace

ConversationRequestController::ConversationRequestController(RequestGuard& guard,
                                                             Hooks hooks,
                                                             QObject* parent)
    : QObject(parent),
      guard_(guard),
      hooks_(std::move(hooks)) {}

void ConversationRequestController::beginSession(QByteArray initialImage,
                                                 const QString& initialUserText) {
    queuedFollowUpTexts_.clear();
    currentSession_ = std::make_shared<chat::ChatSession>();
    currentSession_->beginWithCapture(std::move(initialImage));
    currentSession_->addUserText(initialUserText);
    bindSession();
}

void ConversationRequestController::onFollowUpRequested(const QString& text) {
    if (currentSession_ == nullptr) {
        return;
    }

    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        return;
    }

    if (guard_.state() == BusyState::RequestInFlight) {
        queueFollowUp(trimmedText);
        return;
    }
    if (guard_.isBusy()) {
        return;
    }

    currentSession_->addUserText(trimmedText);
    bindSession();

    if (!sendCurrentSessionRequest(QStringLiteral("Sending follow-up..."),
                                   0,
                                   nullptr,
                                   true)) {
        bindSession();
    }
}

bool ConversationRequestController::startCurrentSessionRequest(
    const QString& busyStatus,
    const int emptyRetryAttempt,
    const config::ProviderProfile* requestProfileOverride,
    const bool allowAssetUploadFallback) {
    return sendCurrentSessionRequest(
        busyStatus,
        emptyRetryAttempt,
        requestProfileOverride,
        allowAssetUploadFallback);
}

void ConversationRequestController::cancelCurrentConversation(const bool clearSession) {
    cancelActiveRequest();

    if (clearSession) {
        currentSession_.reset();
    }
    queuedFollowUpTexts_.clear();

    bindSession();
    setPanelBusy(false, statusForState(guard_.state()));
}

void ConversationRequestController::setRequestStreamStarterForTest(RequestStreamStarter starter) {
    requestStreamStarter_ = std::move(starter);
}

void ConversationRequestController::setEmptyRetryDelayOverrideForTest(const int delayMs) {
    emptyRetryDelayOverrideMs_ = delayMs;
}

int ConversationRequestController::emptyRetryDelayMsForTest(const bool hasImageContext,
                                                            const int emptyRetryAttempt) const {
    return emptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
}

QString ConversationRequestController::queuedFollowUpTextForTest(const int index) const {
    if (index < 0 || index >= queuedFollowUpTexts_.size()) {
        return {};
    }
    return queuedFollowUpTexts_.at(index);
}

int ConversationRequestController::messageCountForTest() const {
    return currentSession_ == nullptr ? 0 : currentSession_->messages().size();
}

QString ConversationRequestController::lastUserMessageTextForTest() const {
    if (currentSession_ == nullptr) {
        return {};
    }

    const auto& messages = currentSession_->messages();
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        if (it->role == chat::ChatRole::User && !it->text.isEmpty()) {
            return it->text;
        }
    }
    return {};
}

QString ConversationRequestController::lastAssistantMessageTextForTest() const {
    if (currentSession_ == nullptr || currentSession_->messages().isEmpty()) {
        return {};
    }

    const auto& messages = currentSession_->messages();
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        if (it->role == chat::ChatRole::Assistant) {
            return it->text;
        }
    }
    return {};
}

QString ConversationRequestController::lastAssistantReasoningForTest() const {
    return currentSession_ == nullptr ? QString() : currentSession_->latestAssistantReasoning();
}

bool ConversationRequestController::sendCurrentSessionRequest(
    const QString& busyStatus,
    const int emptyRetryAttempt,
    const config::ProviderProfile* requestProfileOverride,
    const bool allowAssetUploadFallback) {
    if (currentSession_ == nullptr) {
        return false;
    }

    config::ProviderProfile requestProfile;
    if (requestProfileOverride != nullptr) {
        requestProfile = *requestProfileOverride;
    } else if (hooks_.activeProfileProvider) {
        requestProfile = hooks_.activeProfileProvider();
    }
    requestProfile = withDefaults(std::move(requestProfile));

    const RequestStreamStarter requestStarter = requestStreamStarter_
        ? requestStreamStarter_
        : hooks_.requestStreamStarter;

    if (!requestStarter) {
        return false;
    }

    setPanelBusy(true, busyStatus);

    currentSession_->beginAssistantResponse();
    bindSession();

    const bool started = requestStarter(
        requestProfile,
        currentSession_->messages(),
        [this](QString textDelta) {
            if (currentSession_ != nullptr) {
                currentSession_->appendAssistantTextDelta(textDelta);
            }

            if (currentSession_ != nullptr) {
                scheduleSessionRefresh();
            }
        },
        [this](QString reasoningDelta) {
            if (currentSession_ != nullptr) {
                currentSession_->appendAssistantReasoningDelta(reasoningDelta);
            }

            if (currentSession_ != nullptr) {
                scheduleSessionRefresh();
            }
        },
        [this, emptyRetryAttempt]() {
            QTimer::singleShot(0, this, [this, emptyRetryAttempt]() {
                handleRequestCompleted(emptyRetryAttempt);
            });
        },
        [this, emptyRetryAttempt, requestProfile, allowAssetUploadFallback](QString error) {
            QTimer::singleShot(0, this, [this,
                                         emptyRetryAttempt,
                                         requestProfile,
                                         allowAssetUploadFallback,
                                         error = std::move(error)]() {
                if (currentSession_ != nullptr &&
                    allowAssetUploadFallback &&
                    requestProfile.protocol == config::ProviderProtocol::OpenAiResponses &&
                    messagesContainImage(currentSession_->messages()) &&
                    isAssetUploadFailure(error)) {
                    config::ProviderProfile fallbackProfile = requestProfile;
                    fallbackProfile.protocol = config::ProviderProtocol::OpenAiCompatible;
                    currentSession_->removeLastAssistantMessage();
                    bindSession();
                    syncStatus(QStringLiteral("图片上传失败，正在切换兼容链路重试…"));
                    if (sendCurrentSessionRequest(QStringLiteral("图片上传失败，正在切换兼容链路重试…"),
                                                  emptyRetryAttempt,
                                                  &fallbackProfile,
                                                  false)) {
                        return;
                    }
                }

                const QString friendlyError = QStringLiteral("Request failed: %1").arg(error);
                if (currentSession_ != nullptr) {
                    currentSession_->failAssistantResponse(friendlyError);
                }

                bindSession();
                syncStatus(friendlyError);
            });
        },
        emptyRetryAttempt);

    if (!started) {
        if (currentSession_ != nullptr) {
            currentSession_->failAssistantResponse(QStringLiteral("Unable to send now. The app is busy."));
        }
        bindSession();
        setPanelBusy(false, statusForState(guard_.state()));
        return false;
    }

    syncStatus(busyStatus);
    return true;
}

void ConversationRequestController::queueFollowUp(const QString& text) {
    if (text.isEmpty()) {
        return;
    }

    queuedFollowUpTexts_.append(text);
    syncStatus(QStringLiteral("已排队 %1 条，当前回复结束后自动发送…").arg(queuedFollowUpTexts_.size()));
}

void ConversationRequestController::scheduleQueuedFollowUpSend() {
    if (queuedFollowUpTexts_.isEmpty() || currentSession_ == nullptr || guard_.isBusy()) {
        return;
    }

    const QString nextText = queuedFollowUpTexts_.takeFirst();
    currentSession_->addUserText(nextText);
    bindSession();

    if (!sendCurrentSessionRequest(QStringLiteral("正在发送排队追问…"),
                                   0,
                                   nullptr,
                                   true)) {
        bindSession();
        syncStatus(QStringLiteral("Unable to start AI request"));
    }
}

void ConversationRequestController::handleRequestCompleted(const int emptyRetryAttempt) {
    bool shouldScheduleQueuedFollowUp = false;

    if (currentSession_ != nullptr) {
        currentSession_->finalizeAssistantResponse();
        const auto& messages = currentSession_->messages();
        const bool hasImageContext = messagesContainImage(messages);
        const bool hasEmptyAssistantReply =
            !messages.isEmpty() &&
            messages.constLast().role == chat::ChatRole::Assistant &&
            messages.constLast().text.trimmed().isEmpty() &&
            messages.constLast().reasoningText.trimmed().isEmpty();

        if (hasEmptyAssistantReply && emptyRetryAttempt < kMaxEmptyResponseRetries) {
            const int retryDelayMs = emptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
            cancelActiveRequest();
            currentSession_->removeLastAssistantMessage();
            bindSession();
            syncStatus(QStringLiteral("AI 返回空内容，正在自动重试…"));
            QTimer::singleShot(retryDelayMs, this, [this, nextAttempt = emptyRetryAttempt + 1]() {
                if (!sendCurrentSessionRequest(QStringLiteral("AI 返回空内容，正在自动重试…"),
                                               nextAttempt,
                                               nullptr,
                                               true)) {
                    if (currentSession_ != nullptr) {
                        currentSession_->failAssistantResponse(QStringLiteral("(empty response)"));
                    }
                    bindSession();
                    syncStatus(QStringLiteral("Ready"));
                }
            });
            return;
        }

        if (hasEmptyAssistantReply) {
            currentSession_->failAssistantResponse(QStringLiteral("(empty response)"));
        }

        bindSession();
        shouldScheduleQueuedFollowUp = !queuedFollowUpTexts_.isEmpty();
    }

    syncStatus(QStringLiteral("Ready"));
    if (shouldScheduleQueuedFollowUp) {
        scheduleQueuedFollowUpSend();
    }
}

int ConversationRequestController::emptyRetryDelayMs(const bool hasImageContext,
                                                     const int emptyRetryAttempt) const {
    if (emptyRetryDelayOverrideMs_ >= 0) {
        return emptyRetryDelayOverrideMs_;
    }

    return defaultEmptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
}

QString ConversationRequestController::statusForState(const BusyState state) const {
    if (hooks_.statusForState) {
        return hooks_.statusForState(state);
    }

    return QStringLiteral("Ready");
}

void ConversationRequestController::bindSession() const {
    if (hooks_.bindSession) {
        hooks_.bindSession(currentSession_);
    }
}

void ConversationRequestController::scheduleSessionRefresh() const {
    if (hooks_.scheduleSessionRefresh) {
        hooks_.scheduleSessionRefresh();
    }
}

void ConversationRequestController::setPanelBusy(const bool busy,
                                                 const QString& status) const {
    if (hooks_.setPanelBusy) {
        hooks_.setPanelBusy(busy, status);
    }
}

void ConversationRequestController::syncStatus(const QString& status) const {
    if (hooks_.syncStatus) {
        hooks_.syncStatus(status);
    }
}

void ConversationRequestController::cancelActiveRequest() {
    if (hooks_.cancelActiveRequest) {
        hooks_.cancelActiveRequest();
        return;
    }

    guard_.leave(BusyState::RequestInFlight);
}

}  // namespace ais::app
