#pragma once

#include <optional>

#include <QString>
#include <QStringView>

namespace ais::config {

enum class ProviderProtocol {
    OpenAiChat,
    OpenAiResponses,
    OpenAiCompatible,
    Gemini,
    Claude,
};

[[nodiscard]] inline QString toString(ProviderProtocol protocol) {
    switch (protocol) {
    case ProviderProtocol::OpenAiChat:
        return QStringLiteral("openai_chat");
    case ProviderProtocol::OpenAiResponses:
        return QStringLiteral("openai_responses");
    case ProviderProtocol::OpenAiCompatible:
        return QStringLiteral("openai_compatible");
    case ProviderProtocol::Gemini:
        return QStringLiteral("gemini");
    case ProviderProtocol::Claude:
        return QStringLiteral("claude");
    }

    return QStringLiteral("openai_responses");
}

[[nodiscard]] inline std::optional<ProviderProtocol> providerProtocolFromString(QStringView value) {
    if (value == u"openai_chat") {
        return ProviderProtocol::OpenAiChat;
    }
    if (value == u"openai_responses") {
        return ProviderProtocol::OpenAiResponses;
    }
    if (value == u"openai_compatible") {
        return ProviderProtocol::OpenAiCompatible;
    }
    if (value == u"gemini") {
        return ProviderProtocol::Gemini;
    }
    if (value == u"claude") {
        return ProviderProtocol::Claude;
    }

    return std::nullopt;
}

}  // namespace ais::config
