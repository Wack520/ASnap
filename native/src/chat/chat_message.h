#pragma once

#include <QByteArray>
#include <QString>

namespace ais::chat {

enum class ChatRole {
    User,
    Assistant,
};

struct ChatMessage {
    ChatRole role = ChatRole::User;
    QString text;
    QString reasoningText;
    QByteArray imageBytes;
    bool streaming = false;

    [[nodiscard]] bool hasImage() const noexcept { return !imageBytes.isEmpty(); }
    [[nodiscard]] bool hasReasoning() const noexcept { return !reasoningText.isEmpty(); }

    bool operator==(const ChatMessage&) const = default;
};

}  // namespace ais::chat
