#include "app/conversation_runtime.h"

#include <QBuffer>
#include <QPixmap>
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

ConversationRuntime::ConversationRuntime(Hooks hooks, QObject* parent)
    : QObject(parent),
      hooks_(std::move(hooks)) {}

void ConversationRuntime::setConfig(const config::AppConfig& config) {
    config_ = config;
}

void ConversationRuntime::setRequestStreamStarterForTest(RequestStreamStarter starter) {
    requestStreamStarter_ = std::move(starter);
}

void ConversationRuntime::setEmptyRetryDelayOverrideForTest(const int delayMs) {
    emptyRetryDelayOverrideMs_ = delayMs;
}

int ConversationRuntime::emptyRetryDelayMsForTest(const bool hasImageContext,
                                                  const int emptyRetryAttempt) const {
    return emptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
}

void ConversationRuntime::seedConversationForTest(const QString& initialUserText) {
    currentSession_ = std::make_shared<chat::ChatSession>();
    currentSession_->beginWithCapture(QByteArray("png-image"));
    currentSession_->addUserText(initialUserText);
    queuedFollowUpTexts_.clear();
}

void ConversationRuntime::followUpRequested(const QString& text) {
    if (currentSession_ == nullptr) {
        return;
    }

    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        return;
    }

    if (hooks_.busyState != nullptr && hooks_.busyState() == BusyState::RequestInFlight) {
        queueFollowUp(trimmedText);
        return;
    }
    if (hooks_.isBusy != nullptr && hooks_.isBusy()) {
        return;
    }

    currentSession_->addUserText(trimmedText);
    refreshChatBinding();

    if (!sendCurrentSessionRequest(QStringLiteral("Sending follow-up..."))) {
        refreshChatBinding();
    }
}

void ConversationRuntime::beginSessionFromSelection(const capture::CaptureSelection& selection) {
    queuedFollowUpTexts_.clear();
    currentSession_ = std::make_shared<chat::ChatSession>();
    currentSession_->beginWithCapture(encodePng(selection.image));
    currentSession_->addUserText(defaultFirstPrompt());

    if (hooks_.ensureChatPanel != nullptr) {
        hooks_.ensureChatPanel();
    }
    if (!hasChatPanel()) {
        if (hooks_.syncStatus != nullptr) {
            hooks_.syncStatus(QStringLiteral("Chat panel is unavailable"));
        }
        return;
    }

    refreshChatBinding();
    if (hooks_.showChatPanel != nullptr) {
        hooks_.showChatPanel();
    }
    if (hooks_.placeChatPanelNearSelection != nullptr) {
        hooks_.placeChatPanelNearSelection(selection.virtualRect);
    }
    if (hooks_.raiseChatPanel != nullptr) {
        hooks_.raiseChatPanel();
    }
    if (hooks_.activateChatPanel != nullptr) {
        hooks_.activateChatPanel();
    }

    if (!sendCurrentSessionRequest(QStringLiteral("Analyzing screenshot..."))) {
        refreshChatBinding();
        if (hooks_.syncStatus != nullptr) {
            hooks_.syncStatus(QStringLiteral("Unable to start AI request"));
        }
    }
}

bool ConversationRuntime::sendCurrentSessionRequest(const QString& busyStatus,
                                                    const int emptyRetryAttempt,
                                                    const config::ProviderProfile* requestProfileOverride,
                                                    const bool allowAssetUploadFallback) {
    if (currentSession_ == nullptr) {
        return false;
    }
    const config::ProviderProfile requestProfile =
        withDefaults(requestProfileOverride != nullptr ? *requestProfileOverride
                                                       : config_.activeProfile);

    const RequestStreamStarter requestStarter = requestStreamStarter_
        ? requestStreamStarter_
        : hooks_.requestStreamStarter;

    if (hooks_.setChatBusy != nullptr) {
        hooks_.setChatBusy(true, busyStatus);
    }

    currentSession_->beginAssistantResponse();
    refreshChatBinding();

    const bool started =
        requestStarter != nullptr &&
        requestStarter(
            requestProfile,
            currentSession_->messages(),
            [this](QString textDelta) {
                if (currentSession_ != nullptr) {
                    currentSession_->appendAssistantTextDelta(textDelta);
                }

                if (hooks_.scheduleSessionRefresh != nullptr && currentSession_ != nullptr) {
                    hooks_.scheduleSessionRefresh();
                }
            },
            [this](QString reasoningDelta) {
                if (currentSession_ != nullptr) {
                    currentSession_->appendAssistantReasoningDelta(reasoningDelta);
                }

                if (hooks_.scheduleSessionRefresh != nullptr && currentSession_ != nullptr) {
                    hooks_.scheduleSessionRefresh();
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
                        refreshChatBinding();
                        if (hooks_.syncStatus != nullptr) {
                            hooks_.syncStatus(QStringLiteral("图片上传失败，正在切换兼容链路重试…"));
                        }
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

                    refreshChatBinding();
                    if (hooks_.syncStatus != nullptr) {
                        hooks_.syncStatus(friendlyError);
                    }
                });
            },
            emptyRetryAttempt);

    if (!started) {
        if (currentSession_ != nullptr) {
            currentSession_->failAssistantResponse(QStringLiteral("Unable to send now. The app is busy."));
        }
        refreshChatBinding();
        if (hooks_.setChatBusy != nullptr) {
            hooks_.setChatBusy(false, statusForCurrentState());
        }
        return false;
    }

    if (hooks_.syncStatus != nullptr) {
        hooks_.syncStatus(busyStatus);
    }
    return true;
}

void ConversationRuntime::queueFollowUp(const QString& text) {
    if (text.isEmpty()) {
        return;
    }

    queuedFollowUpTexts_.append(text);
    if (hooks_.syncStatus != nullptr) {
        hooks_.syncStatus(QStringLiteral("已排队 %1 条，当前回复结束后自动发送…").arg(queuedFollowUpTexts_.size()));
    }
}

void ConversationRuntime::scheduleQueuedFollowUpSend() {
    if (queuedFollowUpTexts_.isEmpty() || currentSession_ == nullptr ||
        (hooks_.isBusy != nullptr && hooks_.isBusy())) {
        return;
    }

    const QString nextText = queuedFollowUpTexts_.takeFirst();
    currentSession_->addUserText(nextText);
    refreshChatBinding();

    if (!sendCurrentSessionRequest(QStringLiteral("正在发送排队追问…"))) {
        refreshChatBinding();
        if (hooks_.syncStatus != nullptr) {
            hooks_.syncStatus(QStringLiteral("Unable to start AI request"));
        }
    }
}

void ConversationRuntime::handleRequestCompleted(const int emptyRetryAttempt) {
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
            if (hooks_.cancelActiveRequest != nullptr) {
                hooks_.cancelActiveRequest();
            }
            currentSession_->removeLastAssistantMessage();
            refreshChatBinding();
            if (hooks_.syncStatus != nullptr) {
                hooks_.syncStatus(QStringLiteral("AI 返回空内容，正在自动重试…"));
            }
            QTimer::singleShot(retryDelayMs, this, [this, nextAttempt = emptyRetryAttempt + 1]() {
                if (!sendCurrentSessionRequest(QStringLiteral("AI 返回空内容，正在自动重试…"),
                                               nextAttempt)) {
                    if (currentSession_ != nullptr) {
                        currentSession_->failAssistantResponse(QStringLiteral("(empty response)"));
                    }
                    refreshChatBinding();
                    if (hooks_.syncStatus != nullptr) {
                        hooks_.syncStatus(QStringLiteral("Ready"));
                    }
                }
            });
            return;
        }

        if (hasEmptyAssistantReply) {
            currentSession_->failAssistantResponse(QStringLiteral("(empty response)"));
        }

        refreshChatBinding();
        shouldScheduleQueuedFollowUp = !queuedFollowUpTexts_.isEmpty();
    }

    if (hooks_.syncStatus != nullptr) {
        hooks_.syncStatus(QStringLiteral("Ready"));
    }
    if (shouldScheduleQueuedFollowUp) {
        scheduleQueuedFollowUpSend();
    }
}

void ConversationRuntime::cancelCurrentConversation(const bool clearSession) {
    if (hooks_.cancelActiveRequest != nullptr) {
        hooks_.cancelActiveRequest();
    }

    if (clearSession) {
        currentSession_.reset();
    }
    queuedFollowUpTexts_.clear();

    refreshChatBinding();
    if (hooks_.setChatBusy != nullptr) {
        hooks_.setChatBusy(false, statusForCurrentState());
    }
}

const std::shared_ptr<chat::ChatSession>& ConversationRuntime::currentSession() const noexcept {
    return currentSession_;
}

bool ConversationRuntime::hasCurrentSession() const noexcept {
    return currentSession_ != nullptr;
}

int ConversationRuntime::queuedFollowUpCountForTest() const noexcept {
    return queuedFollowUpTexts_.size();
}

QString ConversationRuntime::queuedFollowUpTextForTest(const int index) const {
    if (index < 0 || index >= queuedFollowUpTexts_.size()) {
        return {};
    }
    return queuedFollowUpTexts_.at(index);
}

int ConversationRuntime::messageCountForTest() const {
    return currentSession_ == nullptr ? 0 : currentSession_->messages().size();
}

QString ConversationRuntime::lastUserMessageTextForTest() const {
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

QString ConversationRuntime::lastAssistantMessageTextForTest() const {
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

QString ConversationRuntime::lastAssistantReasoningForTest() const {
    return currentSession_ == nullptr ? QString() : currentSession_->latestAssistantReasoning();
}

QString ConversationRuntime::defaultFirstPrompt() const {
    const QString prompt = config_.firstPrompt.trimmed();
    if (!prompt.isEmpty()) {
        return prompt;
    }

    return config::defaultFirstPromptText();
}

int ConversationRuntime::emptyRetryDelayMs(const bool hasImageContext,
                                           const int emptyRetryAttempt) const {
    if (emptyRetryDelayOverrideMs_ >= 0) {
        return emptyRetryDelayOverrideMs_;
    }

    return defaultEmptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
}

QByteArray ConversationRuntime::encodePng(const QPixmap& pixmap) const {
    if (pixmap.isNull()) {
        return {};
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return bytes;
}

void ConversationRuntime::refreshChatBinding() const {
    if (hooks_.refreshChatBinding != nullptr) {
        hooks_.refreshChatBinding();
    }
}

bool ConversationRuntime::hasChatPanel() const {
    return hooks_.hasChatPanel == nullptr || hooks_.hasChatPanel();
}

QString ConversationRuntime::statusForCurrentState() const {
    return hooks_.statusForCurrentState != nullptr
        ? hooks_.statusForCurrentState()
        : QStringLiteral("Ready");
}

}  // namespace ais::app
