#include "chat/chat_session.h"

namespace ais::chat {

void ChatSession::beginWithCapture(QByteArray imageBytes) {
    messages_.clear();
    messages_.append(ChatMessage{
        .role = ChatRole::User,
        .text = {},
        .imageBytes = std::move(imageBytes),
    });
}

void ChatSession::addUserText(const QString& text) {
    messages_.append(ChatMessage{
        .role = ChatRole::User,
        .text = text,
    });
}

void ChatSession::addAssistantText(const QString& text) {
    messages_.append(ChatMessage{
        .role = ChatRole::Assistant,
        .text = text,
    });
}

void ChatSession::beginAssistantResponse() {
    if (activeAssistantMessage() != nullptr) {
        return;
    }

    messages_.append(ChatMessage{
        .role = ChatRole::Assistant,
        .streaming = true,
    });
}

void ChatSession::appendAssistantTextDelta(const QString& delta) {
    if (delta.isEmpty()) {
        return;
    }

    beginAssistantResponse();
    messages_.last().text += delta;
}

void ChatSession::appendAssistantReasoningDelta(const QString& delta) {
    if (delta.isEmpty()) {
        return;
    }

    beginAssistantResponse();
    messages_.last().reasoningText += delta;
}

void ChatSession::finalizeAssistantResponse() {
    if (ChatMessage* message = activeAssistantMessage(); message != nullptr) {
        message->streaming = false;
    }
}

void ChatSession::failAssistantResponse(const QString& errorText) {
    if (messages_.isEmpty() || messages_.last().role != ChatRole::Assistant) {
        messages_.append(ChatMessage{
            .role = ChatRole::Assistant,
        });
    }

    messages_.last().text = errorText;
    messages_.last().streaming = false;
}

void ChatSession::removeLastAssistantMessage() {
    if (!messages_.isEmpty() && messages_.last().role == ChatRole::Assistant) {
        messages_.removeLast();
    }
}

QString ChatSession::latestAssistantReasoning() const {
    for (auto it = messages_.crbegin(); it != messages_.crend(); ++it) {
        if (it->role == ChatRole::Assistant && !it->reasoningText.trimmed().isEmpty()) {
            return it->reasoningText;
        }
    }

    return {};
}

ChatMessage* ChatSession::activeAssistantMessage() {
    if (messages_.isEmpty()) {
        return nullptr;
    }

    ChatMessage& last = messages_.last();
    if (last.role != ChatRole::Assistant || !last.streaming) {
        return nullptr;
    }

    return &last;
}

}  // namespace ais::chat
