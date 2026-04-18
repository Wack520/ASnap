#pragma once

#include <QString>

#include "config/provider_protocol.h"

namespace ais::config {

struct ProviderPreset {
    ProviderProtocol protocol = ProviderProtocol::OpenAiResponses;
    QString label;
    QString defaultBaseUrl;
    QString defaultModel;
    QString modelHint;
};

[[nodiscard]] inline ProviderPreset presetFor(ProviderProtocol protocol) {
    switch (protocol) {
    case ProviderProtocol::OpenAiChat:
        return {
            .protocol = protocol,
            .label = QStringLiteral("OpenAI Chat Completions"),
            .defaultBaseUrl = QStringLiteral("https://api.openai.com/v1"),
            .defaultModel = QStringLiteral("gpt-4.1-mini"),
            .modelHint = QStringLiteral("Chat Completions compatible model"),
        };
    case ProviderProtocol::OpenAiResponses:
        return {
            .protocol = protocol,
            .label = QStringLiteral("OpenAI Responses"),
            .defaultBaseUrl = QStringLiteral("https://api.openai.com/v1"),
            .defaultModel = QStringLiteral("gpt-4.1-mini"),
            .modelHint = QStringLiteral("Responses API model"),
        };
    case ProviderProtocol::OpenAiCompatible:
        return {
            .protocol = protocol,
            .label = QStringLiteral("OpenAI Compatible"),
            .defaultBaseUrl = QStringLiteral("https://api.openai.com/v1"),
            .defaultModel = QStringLiteral("gpt-4.1-mini"),
            .modelHint = QStringLiteral("Any OpenAI-compatible chat model"),
        };
    case ProviderProtocol::Gemini:
        return {
            .protocol = protocol,
            .label = QStringLiteral("Gemini"),
            .defaultBaseUrl = QStringLiteral("https://generativelanguage.googleapis.com/v1beta"),
            .defaultModel = QStringLiteral("gemini-2.5-flash"),
            .modelHint = QStringLiteral("Gemini multimodal model"),
        };
    case ProviderProtocol::Claude:
        return {
            .protocol = protocol,
            .label = QStringLiteral("Claude"),
            .defaultBaseUrl = QStringLiteral("https://api.anthropic.com/v1"),
            .defaultModel = QStringLiteral("claude-sonnet-4-0"),
            .modelHint = QStringLiteral("Claude messages model"),
        };
    }

    return {};
}

}  // namespace ais::config
