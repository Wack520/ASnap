#include "ai/ai_client.h"

#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QScopeGuard>
#include <QUuid>

#include "ai/provider_factory.h"

namespace ais::ai {

namespace {

using ais::chat::ChatMessage;
using ais::config::ProviderProfile;
using ais::config::ProviderProtocol;

struct ConversationPlan {
    ProviderProfile profile;
    QList<ChatMessage> messages;
};

[[nodiscard]] ConversationPlan buildConversationPlan(const ProviderProfile& profile,
                                                     const QList<ChatMessage>& messages) {
    return ConversationPlan{
        .profile = profile,
        .messages = messages,
    };
}

[[nodiscard]] int countImages(const QList<ChatMessage>& messages) {
    int count = 0;
    for (const ChatMessage& message : messages) {
        if (message.hasImage()) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] qsizetype totalImageBytes(const QList<ChatMessage>& messages) {
    qsizetype total = 0;
    for (const ChatMessage& message : messages) {
        total += message.imageBytes.size();
    }
    return total;
}

[[nodiscard]] bool supportsStreaming(ProviderProtocol protocol) {
    return protocol == ProviderProtocol::OpenAiChat ||
           protocol == ProviderProtocol::OpenAiCompatible ||
           protocol == ProviderProtocol::OpenAiResponses;
}

[[nodiscard]] bool shouldUseStreamingTransport(const ProviderProtocol protocol,
                                               const QList<ChatMessage>& messages,
                                               const int retryAttempt) {
    if (!supportsStreaming(protocol)) {
        return false;
    }

    if (protocol == ProviderProtocol::OpenAiResponses &&
        retryAttempt > 0 &&
        countImages(messages) > 0) {
        return false;
    }

    return true;
}

[[nodiscard]] QString textFromValue(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }

    if (value.isArray()) {
        QString combined;
        for (const QJsonValue& item : value.toArray()) {
            const QString text = textFromValue(item);
            if (text.isEmpty()) {
                continue;
            }
            if (!combined.isEmpty()) {
                combined += QChar::LineFeed;
            }
            combined += text;
        }
        return combined;
    }

    if (!value.isObject()) {
        return {};
    }

    const QJsonObject object = value.toObject();
    for (const QString& key : {
             QStringLiteral("text"),
             QStringLiteral("output_text"),
             QStringLiteral("reasoning"),
             QStringLiteral("reasoning_text"),
             QStringLiteral("reasoning_content"),
             QStringLiteral("summary_text"),
         }) {
        const QJsonValue candidate = object.value(key);
        if (candidate.isString() && !candidate.toString().isEmpty()) {
            return candidate.toString();
        }
    }

    QString combined;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString text = textFromValue(it.value());
        if (text.isEmpty()) {
            continue;
        }
        if (!combined.isEmpty()) {
            combined += QChar::LineFeed;
        }
        combined += text;
    }
    return combined;
}

[[nodiscard]] RequestSpec withStreamFlag(RequestSpec spec, bool enabled) {
    const QJsonDocument document = QJsonDocument::fromJson(spec.body);
    if (!document.isObject()) {
        return spec;
    }

    QJsonObject root = document.object();
    root.insert(QStringLiteral("stream"), enabled);
    spec.body = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return spec;
}

[[nodiscard]] RequestSpec withRequestMetadata(RequestSpec spec, const int retryAttempt) {
    spec.headers.insert(QStringLiteral("Cache-Control"), QStringLiteral("no-cache"));
    spec.headers.insert(QStringLiteral("Pragma"), QStringLiteral("no-cache"));
    spec.headers.insert(QStringLiteral("X-ASnap-Request-Id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    spec.headers.insert(QStringLiteral("X-ASnap-Retry-Attempt"), QString::number(qMax(0, retryAttempt)));
    return spec;
}

[[nodiscard]] QString extractResponsesReasoning(const QByteArray& payload);

void appendCombinedText(QString* target, const QString& text) {
    if (target == nullptr || text.isEmpty()) {
        return;
    }
    if (!target->isEmpty()) {
        *target += QChar::LineFeed;
    }
    *target += text;
}

[[nodiscard]] QString directTextField(const QJsonObject& object) {
    for (const QString& key : {
             QStringLiteral("text"),
             QStringLiteral("output_text"),
         }) {
        const QJsonValue value = object.value(key);
        const QString text = textFromValue(value);
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

[[nodiscard]] QString extractResponsesTextFromContentParts(const QJsonArray& content) {
    QString combined;
    for (const QJsonValue& partValue : content) {
        const QJsonObject part = partValue.toObject();
        if (part.value(QStringLiteral("type")).toString() != QStringLiteral("output_text")) {
            continue;
        }
        appendCombinedText(&combined, directTextField(part));
    }
    return combined;
}

[[nodiscard]] QString extractResponsesReasoningFromContentParts(const QJsonArray& content) {
    QString combined;
    for (const QJsonValue& partValue : content) {
        const QJsonObject part = partValue.toObject();
        const QString partType = part.value(QStringLiteral("type")).toString();
        if (!partType.contains(QStringLiteral("reasoning")) &&
            !partType.contains(QStringLiteral("summary"))) {
            continue;
        }
        QString text = directTextField(part);
        if (text.isEmpty()) {
            text = textFromValue(part);
        }
        appendCombinedText(&combined, text);
    }
    return combined;
}

[[nodiscard]] QString extractResponsesTextFromOutputItem(const QJsonObject& item) {
    const QString itemType = item.value(QStringLiteral("type")).toString();
    if (itemType == QStringLiteral("message")) {
        return extractResponsesTextFromContentParts(item.value(QStringLiteral("content")).toArray());
    }
    if (itemType == QStringLiteral("output_text")) {
        return directTextField(item);
    }
    return {};
}

[[nodiscard]] QString extractResponsesReasoningFromOutputItem(const QJsonObject& item) {
    const QString itemType = item.value(QStringLiteral("type")).toString();
    if (itemType == QStringLiteral("reasoning")) {
        QString text = textFromValue(item.value(QStringLiteral("summary")));
        if (text.isEmpty()) {
            text = textFromValue(item.value(QStringLiteral("content")));
        }
        return text;
    }
    if (itemType == QStringLiteral("message")) {
        return extractResponsesReasoningFromContentParts(item.value(QStringLiteral("content")).toArray());
    }
    return {};
}

[[nodiscard]] QString extractResponsesTextFromResponseObject(const QJsonObject& response) {
    const QString outputText = response.value(QStringLiteral("output_text")).toString();
    if (!outputText.isEmpty()) {
        return outputText;
    }

    QString combined;
    for (const QJsonValue& itemValue : response.value(QStringLiteral("output")).toArray()) {
        appendCombinedText(&combined, extractResponsesTextFromOutputItem(itemValue.toObject()));
    }
    return combined;
}

void appendResponsesFallbacksFromObject(const QJsonObject& object,
                                        QString* fallbackText,
                                        QString* fallbackReasoning) {
    if (object.isEmpty()) {
        return;
    }

    if (const QJsonObject response = object.value(QStringLiteral("response")).toObject();
        !response.isEmpty()) {
        appendCombinedText(fallbackText, extractResponsesTextFromResponseObject(response));
        appendCombinedText(
            fallbackReasoning,
            extractResponsesReasoning(QJsonDocument(response).toJson(QJsonDocument::Compact)));
    }

    appendCombinedText(fallbackText, extractResponsesTextFromResponseObject(object));
    appendCombinedText(
        fallbackReasoning,
        extractResponsesReasoning(QJsonDocument(object).toJson(QJsonDocument::Compact)));

    if (const QJsonObject item = object.value(QStringLiteral("item")).toObject(); !item.isEmpty()) {
        appendCombinedText(fallbackText, extractResponsesTextFromOutputItem(item));
        appendCombinedText(fallbackReasoning, extractResponsesReasoningFromOutputItem(item));
    }

    if (const QJsonObject part = object.value(QStringLiteral("part")).toObject(); !part.isEmpty()) {
        if (part.value(QStringLiteral("type")).toString() == QStringLiteral("output_text")) {
            appendCombinedText(fallbackText, directTextField(part));
        }
        appendCombinedText(fallbackReasoning,
                           extractResponsesReasoningFromContentParts(QJsonArray{part}));
    }
}

[[nodiscard]] QString extractResponsesReasoning(const QByteArray& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return {};
    }

    QString combined;
    const QJsonArray output = document.object().value(QStringLiteral("output")).toArray();
    for (const QJsonValue& itemValue : output) {
        appendCombinedText(&combined, extractResponsesReasoningFromOutputItem(itemValue.toObject()));
    }

    return combined;
}

[[nodiscard]] QString extractResponsesReasoningDelta(const QJsonObject& event) {
    if (!event.value(QStringLiteral("type")).toString().contains(QStringLiteral("reasoning"))) {
        return {};
    }

    const QString text = directTextField(event);
    if (!text.isEmpty()) {
        return text;
    }

    const QJsonValue delta = event.value(QStringLiteral("delta"));
    if (delta.isString()) {
        return delta.toString();
    }

    return textFromValue(delta);
}

void consumeSseBlocks(QByteArray& pending,
                      const std::function<void(QByteArray)>& handleBlock,
                      bool flushRemainder = false) {
    while (true) {
        int separatorIndex = pending.indexOf("\n\n");
        int separatorLength = 2;
        const int windowsSeparatorIndex = pending.indexOf("\r\n\r\n");
        if (separatorIndex < 0 || (windowsSeparatorIndex >= 0 && windowsSeparatorIndex < separatorIndex)) {
            separatorIndex = windowsSeparatorIndex;
            separatorLength = 4;
        }

        if (separatorIndex < 0) {
            if (flushRemainder && !pending.trimmed().isEmpty()) {
                handleBlock(pending);
                pending.clear();
            }
            return;
        }

        const QByteArray block = pending.left(separatorIndex);
        pending.remove(0, separatorIndex + separatorLength);
        if (!block.trimmed().isEmpty()) {
            handleBlock(block);
        }
    }
}

struct SseEvent {
    QString eventName;
    QByteArray data;
};

[[nodiscard]] SseEvent parseSseEvent(const QByteArray& block) {
    SseEvent event;
    QList<QByteArray> dataLines;

    for (QByteArray line : block.split('\n')) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }

        if (line.startsWith("event:")) {
            event.eventName = QString::fromUtf8(line.mid(6)).trimmed();
            continue;
        }

        if (line.startsWith("data:")) {
            dataLines.append(line.mid(5).trimmed());
        }
    }

    event.data = dataLines.join("\n");
    return event;
}

[[nodiscard]] QString extractStreamErrorMessage(const QString& eventName,
                                                const QJsonObject& root) {
    const QJsonObject errorObject = root.value(QStringLiteral("error")).toObject();
    QString message = textFromValue(errorObject.value(QStringLiteral("message")));
    if (message.isEmpty()) {
        message = textFromValue(root.value(QStringLiteral("message")));
    }
    if (message.isEmpty() && !errorObject.isEmpty()) {
        message = textFromValue(errorObject);
    }

    const QString type = root.value(QStringLiteral("type")).toString();
    const bool looksLikeError =
        eventName.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
        type.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
        type.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
        root.contains(QStringLiteral("error"));

    if (!looksLikeError) {
        return {};
    }

    return message.isEmpty() ? QStringLiteral("Upstream request failed") : message;
}

void emitResponsesStreamDeltas(const QJsonObject& event,
                               const AiClient::DeltaHandler& onTextDelta,
                               const AiClient::DeltaHandler& onReasoningDelta,
                               bool* sawTextDelta,
                               bool* sawReasoningDelta,
                               QString* fallbackText,
                               QString* fallbackReasoning) {
    const QString type = event.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("response.output_text.delta")) {
        const QString delta = textFromValue(event.value(QStringLiteral("delta")));
        if (!delta.isEmpty() && onTextDelta) {
            *sawTextDelta = true;
            onTextDelta(delta);
        }
        return;
    }

    if (type == QStringLiteral("response.output_text.done")) {
        const QString text = directTextField(event);
        appendCombinedText(fallbackText, text);
        return;
    }

    if (type == QStringLiteral("response.content_part.done")) {
        const QJsonObject part = event.value(QStringLiteral("part")).toObject();
        const QString partType = part.value(QStringLiteral("type")).toString();
        if (partType == QStringLiteral("output_text")) {
            appendCombinedText(fallbackText, directTextField(part));
            return;
        }

        appendCombinedText(fallbackReasoning,
                           extractResponsesReasoningFromContentParts(QJsonArray{part}));
        return;
    }

    if (type == QStringLiteral("response.output_item.done")) {
        const QJsonObject item = event.value(QStringLiteral("item")).toObject();
        appendCombinedText(fallbackText, extractResponsesTextFromOutputItem(item));
        appendCombinedText(fallbackReasoning, extractResponsesReasoningFromOutputItem(item));
        return;
    }

    if (type == QStringLiteral("response.completed") || type == QStringLiteral("response.done")) {
        const QJsonObject response = event.value(QStringLiteral("response")).toObject();
        if (!response.isEmpty()) {
            appendCombinedText(fallbackText, extractResponsesTextFromResponseObject(response));
            appendCombinedText(
                fallbackReasoning,
                extractResponsesReasoning(QJsonDocument(response).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    const QString reasoningText = extractResponsesReasoningDelta(event);
    if (reasoningText.isEmpty()) {
        return;
    }

    if (type.endsWith(QStringLiteral(".delta"))) {
        if (onReasoningDelta) {
            *sawReasoningDelta = true;
            onReasoningDelta(reasoningText);
        }
        return;
    }

        appendCombinedText(fallbackReasoning, reasoningText);
}

void emitChatStreamDeltas(const QJsonObject& root,
                          const AiClient::DeltaHandler& onTextDelta,
                          const AiClient::DeltaHandler& onReasoningDelta,
                          bool* sawTextDelta,
                          bool* sawReasoningDelta) {
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    for (const QJsonValue& choiceValue : choices) {
        const QJsonObject delta = choiceValue.toObject().value(QStringLiteral("delta")).toObject();

        const QString textDelta = textFromValue(delta.value(QStringLiteral("content")));
        if (!textDelta.isEmpty() && onTextDelta) {
            *sawTextDelta = true;
            onTextDelta(textDelta);
        }

        for (const QString& key : {
                 QStringLiteral("reasoning_content"),
                 QStringLiteral("reasoning"),
                 QStringLiteral("reasoning_text"),
             }) {
            const QString reasoningDelta = textFromValue(delta.value(key));
            if (!reasoningDelta.isEmpty() && onReasoningDelta) {
                *sawReasoningDelta = true;
                onReasoningDelta(reasoningDelta);
            }
        }
    }
}

}  // namespace

AiClient::AiClient(std::unique_ptr<INetworkTransport> transport, app::RequestGuard& guard)
    : transport_(std::move(transport)),
      guard_(guard) {}

AiClient::~AiClient() = default;

bool AiClient::sendConversation(const config::ProviderProfile& profile,
                                const QList<chat::ChatMessage>& messages,
                                SuccessHandler onSuccess,
                                FailureHandler onFailure) {
    if (!transport_) {
        if (onFailure) {
            onFailure(QStringLiteral("AI transport is not configured"));
        }
        return false;
    }

    if (!guard_.tryEnter(app::BusyState::RequestInFlight)) {
        return false;
    }
    activeRequest_ = std::make_shared<RequestState>();

    const ConversationPlan plan = buildConversationPlan(profile, messages);
    auto providerHandle = makeProvider(plan.profile.protocol);
    if (!providerHandle) {
        activeRequest_.reset();
        guard_.leave(app::BusyState::RequestInFlight);
        if (onFailure) {
            onFailure(QStringLiteral("Unsupported provider protocol"));
        }
        return false;
    }

    const auto provider = std::shared_ptr<IProvider>(providerHandle.release());
    const RequestSpec spec = withStreamFlag(provider->buildRequest(plan.profile, plan.messages), false);
    app::RequestGuard* const guard = &guard_;
    const auto requestState = activeRequest_;
    transport_->post(
        spec,
        [guard, requestState, provider, onSuccess = std::move(onSuccess)](QByteArray payload) mutable {
            const auto release = qScopeGuard([guard] { guard->leave(app::BusyState::RequestInFlight); });
            if (requestState == nullptr || requestState->cancelled) {
                return;
            }
            const QString text = provider->parseTextResponse(payload);
            if (onSuccess) {
                onSuccess(text);
            }
        },
        [guard, requestState, onFailure = std::move(onFailure)](QString error) mutable {
            const auto release = qScopeGuard([guard] { guard->leave(app::BusyState::RequestInFlight); });
            if (requestState == nullptr || requestState->cancelled) {
                return;
            }
            if (onFailure) {
                onFailure(std::move(error));
            }
        });

    return true;
}

bool AiClient::sendConversationStream(const config::ProviderProfile& profile,
                                      const QList<chat::ChatMessage>& messages,
                                      DeltaHandler onTextDelta,
                                      DeltaHandler onReasoningDelta,
                                      CompletionHandler onComplete,
                                      FailureHandler onFailure,
                                      const int retryAttempt) {
    if (!transport_) {
        if (onFailure) {
            onFailure(QStringLiteral("AI transport is not configured"));
        }
        return false;
    }

    if (!guard_.tryEnter(app::BusyState::RequestInFlight)) {
        return false;
    }
    activeRequest_ = std::make_shared<RequestState>();

    const ConversationPlan plan = buildConversationPlan(profile, messages);
    auto providerHandle = makeProvider(plan.profile.protocol);
    if (!providerHandle) {
        activeRequest_.reset();
        guard_.leave(app::BusyState::RequestInFlight);
        if (onFailure) {
            onFailure(QStringLiteral("Unsupported provider protocol"));
        }
        return false;
    }

    if (!shouldUseStreamingTransport(plan.profile.protocol, plan.messages, retryAttempt)) {
        const auto provider = std::shared_ptr<IProvider>(providerHandle.release());
        const RequestSpec spec = withRequestMetadata(
            withStreamFlag(provider->buildRequest(plan.profile, plan.messages), false),
            retryAttempt);
        qInfo().noquote()
            << QStringLiteral("ASnap: sending request to %1 (stream=false, messages=%2, images=%3, imageBytes=%4, bodyBytes=%5, retry=%6)")
                   .arg(spec.url.toString())
                   .arg(plan.messages.size())
                   .arg(countImages(plan.messages))
                   .arg(totalImageBytes(plan.messages))
                   .arg(spec.body.size())
                   .arg(retryAttempt);
        app::RequestGuard* const guard = &guard_;
        const auto requestState = activeRequest_;
        transport_->post(
            spec,
            [guard, requestState, provider, effectiveProtocol = plan.profile.protocol, onTextDelta = std::move(onTextDelta), onReasoningDelta = std::move(onReasoningDelta), onComplete = std::move(onComplete)](QByteArray payload) mutable {
                const auto releaseGuard = qScopeGuard([guard] { guard->leave(app::BusyState::RequestInFlight); });
                if (requestState == nullptr || requestState->cancelled) {
                    return;
                }
                const QString text = provider->parseTextResponse(payload);
                if (!text.isEmpty() && onTextDelta) {
                    onTextDelta(text);
                }
                if (effectiveProtocol == ProviderProtocol::OpenAiResponses && onReasoningDelta) {
                    const QString reasoning = extractResponsesReasoning(payload);
                    if (!reasoning.isEmpty()) {
                        onReasoningDelta(reasoning);
                    }
                }
                if (onComplete) {
                    onComplete();
                }
            },
            [guard, requestState, onFailure = std::move(onFailure)](QString error) mutable {
                const auto releaseGuard = qScopeGuard([guard] { guard->leave(app::BusyState::RequestInFlight); });
                if (requestState == nullptr || requestState->cancelled) {
                    return;
                }
                if (onFailure) {
                    onFailure(std::move(error));
                }
            });
        return true;
    }

    const auto provider = std::shared_ptr<IProvider>(providerHandle.release());
    const RequestSpec spec = withRequestMetadata(
        withStreamFlag(provider->buildRequest(plan.profile, plan.messages), true),
        retryAttempt);
    qInfo().noquote()
        << QStringLiteral("ASnap: sending request to %1 (stream=true, messages=%2, images=%3, imageBytes=%4, bodyBytes=%5, retry=%6)")
               .arg(spec.url.toString())
               .arg(plan.messages.size())
               .arg(countImages(plan.messages))
               .arg(totalImageBytes(plan.messages))
               .arg(spec.body.size())
               .arg(retryAttempt);
    app::RequestGuard* const guard = &guard_;
    auto fullPayload = std::make_shared<QByteArray>();
    auto pendingBuffer = std::make_shared<QByteArray>();
    auto sawStreamEvent = std::make_shared<bool>(false);
    auto sawTextDelta = std::make_shared<bool>(false);
    auto sawReasoningDelta = std::make_shared<bool>(false);
    auto streamErrorMessage = std::make_shared<QString>();
    auto responsesFallbackText = std::make_shared<QString>();
    auto responsesFallbackReasoning = std::make_shared<QString>();
    auto streamFailureHandler = std::make_shared<FailureHandler>(onFailure);
    const auto requestState = activeRequest_;

    const auto processSseBlock =
        [effectiveProtocol = plan.profile.protocol,
         requestState,
         onTextDelta,
         onReasoningDelta,
         sawStreamEvent,
         sawTextDelta,
         sawReasoningDelta,
         streamErrorMessage,
         responsesFallbackText,
         responsesFallbackReasoning](QByteArray block) mutable {
            if (requestState == nullptr || requestState->cancelled) {
                return;
            }
            if (!streamErrorMessage->isEmpty()) {
                return;
            }
            const SseEvent event = parseSseEvent(block);
            if (event.data.isEmpty() || event.data == "[DONE]") {
                return;
            }

            const QJsonDocument document = QJsonDocument::fromJson(event.data);
            if (!document.isObject()) {
                return;
            }

            *sawStreamEvent = true;
            const QJsonObject root = document.object();
            if (const QString error = extractStreamErrorMessage(event.eventName, root);
                !error.isEmpty()) {
                *streamErrorMessage = error;
                return;
            }
            if (effectiveProtocol == ProviderProtocol::OpenAiResponses) {
                emitResponsesStreamDeltas(root,
                                          onTextDelta,
                                          onReasoningDelta,
                                          sawTextDelta.get(),
                                          sawReasoningDelta.get(),
                                          responsesFallbackText.get(),
                                          responsesFallbackReasoning.get());
                if (root.contains(QStringLiteral("choices"))) {
                    emitChatStreamDeltas(root,
                                         onTextDelta,
                                         onReasoningDelta,
                                         sawTextDelta.get(),
                                         sawReasoningDelta.get());
                }
                if (root.value(QStringLiteral("type")).toString().isEmpty()) {
                    appendResponsesFallbacksFromObject(root,
                                                       responsesFallbackText.get(),
                                                       responsesFallbackReasoning.get());
                }
                return;
            }

            emitChatStreamDeltas(root,
                                 onTextDelta,
                                 onReasoningDelta,
                                 sawTextDelta.get(),
                                 sawReasoningDelta.get());
        };

    transport_->postStream(
        spec,
        [fullPayload, pendingBuffer, processSseBlock](QByteArray chunk) mutable {
            fullPayload->append(chunk);
            pendingBuffer->append(chunk);
            consumeSseBlocks(*pendingBuffer, processSseBlock, false);
        },
        [guard,
         requestState,
         provider,
         effectiveProtocol = plan.profile.protocol,
         fullPayload,
         pendingBuffer,
         sawStreamEvent,
         sawTextDelta,
         sawReasoningDelta,
         streamErrorMessage,
         responsesFallbackText,
         responsesFallbackReasoning,
         streamFailureHandler,
         processSseBlock,
         onTextDelta = std::move(onTextDelta),
         onReasoningDelta = std::move(onReasoningDelta),
         onComplete = std::move(onComplete)]() mutable {
            if (requestState == nullptr || requestState->cancelled) {
                guard->leave(app::BusyState::RequestInFlight);
                return;
            }
            consumeSseBlocks(*pendingBuffer, processSseBlock, true);

            if (!streamErrorMessage->isEmpty()) {
                guard->leave(app::BusyState::RequestInFlight);
                if (*streamFailureHandler) {
                    (*streamFailureHandler)(*streamErrorMessage);
                }
                return;
            }

            if (!*sawStreamEvent) {
                const QString text = provider->parseTextResponse(*fullPayload);
                if (!text.isEmpty() && onTextDelta) {
                    *sawTextDelta = true;
                    onTextDelta(text);
                }
            }

            if (effectiveProtocol == ProviderProtocol::OpenAiResponses) {
                if (!*sawTextDelta && !responsesFallbackText->isEmpty() && onTextDelta) {
                    *sawTextDelta = true;
                    onTextDelta(*responsesFallbackText);
                }

                if (!*sawReasoningDelta) {
                    const QString reasoning = !responsesFallbackReasoning->isEmpty()
                        ? *responsesFallbackReasoning
                        : extractResponsesReasoning(*fullPayload);
                    if (!reasoning.isEmpty() && onReasoningDelta) {
                        *sawReasoningDelta = true;
                        onReasoningDelta(reasoning);
                    }
                }

                if (!*sawTextDelta && !*sawReasoningDelta && !fullPayload->trimmed().isEmpty()) {
                    qWarning().noquote()
                        << "ASnap: Responses stream completed without parsed text/reasoning. Payload snippet:"
                        << QString::fromUtf8(fullPayload->left(1200));
                }
            }

            guard->leave(app::BusyState::RequestInFlight);
            if (onComplete) {
                onComplete();
            }
        },
        [guard, requestState, streamFailureHandler](QString error) mutable {
            guard->leave(app::BusyState::RequestInFlight);
            if (requestState == nullptr || requestState->cancelled) {
                return;
            }
            if (*streamFailureHandler) {
                (*streamFailureHandler)(std::move(error));
            }
        });

    return true;
}

void AiClient::cancelActiveRequest() {
    if (activeRequest_ != nullptr) {
        activeRequest_->cancelled = true;
        activeRequest_.reset();
    }

    if (transport_) {
        transport_->cancelActiveRequest();
    }
    guard_.leave(app::BusyState::RequestInFlight);
}

}  // namespace ais::ai
