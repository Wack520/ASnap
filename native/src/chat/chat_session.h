#pragma once

#include <QList>
#include <QByteArray>
#include <QString>

#include "chat/chat_message.h"

namespace ais::chat {

class ChatSession {
public:
    void beginWithCapture(QByteArray imageBytes);
    void addUserText(const QString& text);
    void addAssistantText(const QString& text);
    void beginAssistantResponse();
    void appendAssistantTextDelta(const QString& delta);
    void appendAssistantReasoningDelta(const QString& delta);
    void finalizeAssistantResponse();
    void failAssistantResponse(const QString& errorText);
    void removeLastAssistantMessage();

    [[nodiscard]] const QList<ChatMessage>& messages() const noexcept { return messages_; }
    [[nodiscard]] QString latestAssistantReasoning() const;

private:
    [[nodiscard]] ChatMessage* activeAssistantMessage();

    QList<ChatMessage> messages_;
};

}  // namespace ais::chat
