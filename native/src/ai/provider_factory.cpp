#include "ai/provider_factory.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QUrl>
#include <QUrlQuery>

namespace {

using ais::chat::ChatMessage;
using ais::chat::ChatRole;
using ais::config::ProviderProfile;
using ais::config::ProviderProtocol;

struct InlineImagePayload {
    QString mimeType = QStringLiteral("image/png");
    QByteArray bytes;
};

[[nodiscard]] QString dataUriFor(const InlineImagePayload& image) {
    return QStringLiteral("data:%1;base64,%2")
        .arg(image.mimeType,
             QString::fromLatin1(image.bytes.toBase64()));
}

[[nodiscard]] QByteArray encodeImage(QImage image,
                                     const char* format,
                                     const int quality = -1) {
    if (image.isNull()) {
        return {};
    }

    if (qstrcmp(format, "JPEG") == 0) {
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        {
            QPainter painter(&flattened);
            painter.drawImage(QPoint(0, 0), image);
        }
        image = flattened;
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }

    image.save(&buffer, format, quality);
    return bytes;
}

[[nodiscard]] InlineImagePayload makeInlineImagePayload(const QByteArray& imageBytes,
                                                        const bool preferXaiCompatibleImage) {
    InlineImagePayload payload{
        .mimeType = QStringLiteral("image/png"),
        .bytes = imageBytes,
    };
    if (!preferXaiCompatibleImage || imageBytes.isEmpty()) {
        return payload;
    }

    constexpr int kLargeInlineImageThresholdBytes = 2 * 1024 * 1024;
    constexpr int kMaxPreferredDimension = 2300;
    constexpr int kPreferredTargetBytes = 4 * 1024 * 1024;

    QImage image = QImage::fromData(imageBytes);
    if (image.isNull()) {
        return payload;
    }

    const bool dimensionsTooLarge =
        std::max(image.width(), image.height()) > kMaxPreferredDimension;
    if (dimensionsTooLarge) {
        image = image.scaled(kMaxPreferredDimension,
                             kMaxPreferredDimension,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    if (!dimensionsTooLarge &&
        payload.bytes.size() <= kLargeInlineImageThresholdBytes) {
        return payload;
    }

    const QByteArray scaledPngBytes = encodeImage(image, "PNG");
    if (!scaledPngBytes.isEmpty() &&
        (dimensionsTooLarge || scaledPngBytes.size() < payload.bytes.size())) {
        payload.bytes = scaledPngBytes;
    }

    for (const int quality : {92, 86, 80, 74, 68}) {
        const QByteArray jpegBytes = encodeImage(image, "JPEG", quality);
        if (jpegBytes.isEmpty()) {
            continue;
        }

        if (jpegBytes.size() <= kPreferredTargetBytes || quality == 68) {
            payload.mimeType = QStringLiteral("image/jpeg");
            payload.bytes = jpegBytes;
            return payload;
        }
    }

    return payload;
}

[[nodiscard]] bool isLikelyXaiLikeProfile(const ProviderProfile& profile) {
    const QString model = profile.model.trimmed();
    if (model.contains(QStringLiteral("grok"), Qt::CaseInsensitive)) {
        return true;
    }

    const QUrl url(profile.baseUrl);
    const QString host = url.host();
    const QString baseUrl = profile.baseUrl;
    return host.contains(QStringLiteral("x.ai"), Qt::CaseInsensitive) ||
           host.contains(QStringLiteral("grok"), Qt::CaseInsensitive) ||
           baseUrl.contains(QStringLiteral("x.ai"), Qt::CaseInsensitive) ||
           baseUrl.contains(QStringLiteral("grok"), Qt::CaseInsensitive);
}

[[nodiscard]] QString messageRole(ChatRole role) {
    return role == ChatRole::Assistant ? QStringLiteral("assistant") : QStringLiteral("user");
}

[[nodiscard]] QString geminiRole(ChatRole role) {
    return role == ChatRole::Assistant ? QStringLiteral("model") : QStringLiteral("user");
}

[[nodiscard]] QUrl appendPath(const QString& baseUrl, QStringView relativePath) {
    QUrl url(baseUrl);

    QString path = url.path();
    if (!path.endsWith(u'/')) {
        path.append(u'/');
    }

    QString normalized = relativePath.toString();
    while (normalized.startsWith(u'/')) {
        normalized.remove(0, 1);
    }

    path.append(normalized);
    url.setPath(path);
    url.setQuery(QString());
    return url;
}

[[nodiscard]] QJsonArray buildOpenAiChatContent(const ChatMessage& message,
                                                const bool preferHighDetailImage) {
    QJsonArray content;

    if (!message.text.isEmpty()) {
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), message.text},
        });
    }

    if (message.hasImage()) {
        const InlineImagePayload imagePayload =
            makeInlineImagePayload(message.imageBytes, preferHighDetailImage);
        QJsonObject imageUrl{
            {QStringLiteral("url"), dataUriFor(imagePayload)},
        };
        if (preferHighDetailImage) {
            imageUrl.insert(QStringLiteral("detail"), QStringLiteral("high"));
        }
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"), imageUrl},
        });
    }

    return content;
}

[[nodiscard]] QJsonArray buildResponsesContent(const ChatMessage& message,
                                               const bool preferHighDetailImage) {
    QJsonArray content;

    if (!message.text.isEmpty()) {
        content.append(QJsonObject{
            {QStringLiteral("type"),
             message.role == ChatRole::Assistant
                 ? QStringLiteral("output_text")
                 : QStringLiteral("input_text")},
            {QStringLiteral("text"), message.text},
        });
    }

    if (message.hasImage()) {
        const InlineImagePayload imagePayload =
            makeInlineImagePayload(message.imageBytes, preferHighDetailImage);
        QJsonObject imagePart{
            {QStringLiteral("type"), QStringLiteral("input_image")},
            {QStringLiteral("image_url"), dataUriFor(imagePayload)},
        };
        if (preferHighDetailImage) {
            imagePart.insert(QStringLiteral("detail"), QStringLiteral("high"));
        }
        content.append(imagePart);
    }

    return content;
}

[[nodiscard]] QJsonValue buildResponsesMessageContent(const ChatMessage& message,
                                                      const bool preferHighDetailImage) {
    if (message.role == ChatRole::Assistant &&
        !message.text.isEmpty() &&
        !message.hasImage()) {
        return QJsonValue(message.text);
    }

    const QJsonArray content = buildResponsesContent(message, preferHighDetailImage);
    if (content.isEmpty()) {
        return QJsonValue(QJsonValue::Undefined);
    }

    return QJsonValue(content);
}

[[nodiscard]] bool messagesContainImage(const QList<ChatMessage>& messages) {
    for (const ChatMessage& message : messages) {
        if (message.hasImage()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] QJsonArray buildGeminiParts(const ChatMessage& message) {
    QJsonArray parts;

    if (!message.text.isEmpty()) {
        parts.append(QJsonObject{
            {QStringLiteral("text"), message.text},
        });
    }

    if (message.hasImage()) {
        parts.append(QJsonObject{
            {QStringLiteral("inline_data"),
             QJsonObject{
                 {QStringLiteral("mime_type"), QStringLiteral("image/png")},
                 {QStringLiteral("data"), QString::fromLatin1(message.imageBytes.toBase64())},
             }},
        });
    }

    return parts;
}

[[nodiscard]] QJsonArray buildClaudeContent(const ChatMessage& message) {
    QJsonArray content;

    if (!message.text.isEmpty()) {
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), message.text},
        });
    }

    if (message.hasImage()) {
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image")},
            {QStringLiteral("source"),
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("base64")},
                 {QStringLiteral("media_type"), QStringLiteral("image/png")},
                 {QStringLiteral("data"), QString::fromLatin1(message.imageBytes.toBase64())},
             }},
        });
    }

    return content;
}

[[nodiscard]] QString textFromValue(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }

    if (value.isArray()) {
        for (const auto& item : value.toArray()) {
            const QString text = textFromValue(item);
            if (!text.isEmpty()) {
                return text;
            }
        }
        return {};
    }

    if (!value.isObject()) {
        return {};
    }

    const QJsonObject object = value.toObject();
    for (const auto key : {QStringLiteral("text"), QStringLiteral("output_text")}) {
        const auto it = object.find(key);
        if (it != object.end() && it->isString()) {
            return it->toString();
        }
    }

    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!it->isArray() && !it->isObject()) {
            continue;
        }

        const QString text = textFromValue(it.value());
        if (!text.isEmpty()) {
            return text;
        }
    }

    return {};
}

class OpenAiChatProvider final : public ais::ai::IProvider {
public:
    [[nodiscard]] ais::ai::RequestSpec buildRequest(const ProviderProfile& profile,
                                                    const QList<ChatMessage>& messages) const override {
        const bool preferHighDetailImage = isLikelyXaiLikeProfile(profile);
        QJsonArray requestMessages;
        for (const auto& message : messages) {
            const QJsonArray content = buildOpenAiChatContent(message, preferHighDetailImage);
            if (content.isEmpty()) {
                continue;
            }

            requestMessages.append(QJsonObject{
                {QStringLiteral("role"), messageRole(message.role)},
                {QStringLiteral("content"), content},
            });
        }

        return ais::ai::RequestSpec{
            .url = appendPath(profile.baseUrl, u"chat/completions"),
            .headers =
                {
                    {QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(profile.apiKey)},
                    {QStringLiteral("Content-Type"), QStringLiteral("application/json")},
                },
            .body = QJsonDocument(QJsonObject{
                                      {QStringLiteral("model"), profile.model},
                                      {QStringLiteral("stream"), false},
                                      {QStringLiteral("messages"), requestMessages},
                                  })
                        .toJson(QJsonDocument::Compact),
        };
    }

    [[nodiscard]] QString parseTextResponse(const QByteArray& payload) const override {
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            return {};
        }

        const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            return {};
        }

        const QJsonObject message = choices.at(0).toObject().value(QStringLiteral("message")).toObject();
        const QJsonValue content = message.value(QStringLiteral("content"));
        if (content.isString()) {
            return content.toString();
        }

        return textFromValue(content);
    }
};

class OpenAiResponsesProvider final : public ais::ai::IProvider {
public:
    [[nodiscard]] ais::ai::RequestSpec buildRequest(const ProviderProfile& profile,
                                                    const QList<ChatMessage>& messages) const override {
        const bool preferHighDetailImage = isLikelyXaiLikeProfile(profile);
        QJsonArray input;
        for (const auto& message : messages) {
            const QJsonValue content = buildResponsesMessageContent(message, preferHighDetailImage);
            if (content.isUndefined()) {
                continue;
            }

            const QString role = messageRole(message.role);
            if (!input.isEmpty() && content.isArray()) {
                QJsonObject previousMessage = input.last().toObject();
                if (previousMessage.value(QStringLiteral("role")).toString() == role &&
                    previousMessage.value(QStringLiteral("content")).isArray()) {
                    QJsonArray mergedContent = previousMessage.value(QStringLiteral("content")).toArray();
                    for (const QJsonValue& part : content.toArray()) {
                        mergedContent.append(part);
                    }
                    previousMessage.insert(QStringLiteral("content"), mergedContent);
                    input[input.size() - 1] = previousMessage;
                    continue;
                }
            }

            QJsonObject requestMessage{
                {QStringLiteral("role"), role},
                {QStringLiteral("content"), content},
                {QStringLiteral("type"), QStringLiteral("message")},
            };
            if (message.role == ChatRole::Assistant) {
                requestMessage.insert(QStringLiteral("phase"), QStringLiteral("final_answer"));
            }

            input.append(requestMessage);
        }

        QJsonObject root{
            {QStringLiteral("model"), profile.model},
            {QStringLiteral("stream"), false},
            {QStringLiteral("input"), input},
        };
        if (messagesContainImage(messages) && isLikelyXaiLikeProfile(profile)) {
            root.insert(QStringLiteral("store"), false);
        }

        return ais::ai::RequestSpec{
            .url = appendPath(profile.baseUrl, u"responses"),
            .headers =
                {
                    {QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(profile.apiKey)},
                    {QStringLiteral("Content-Type"), QStringLiteral("application/json")},
                },
            .body = QJsonDocument(root)
                        .toJson(QJsonDocument::Compact),
        };
    }

    [[nodiscard]] QString parseTextResponse(const QByteArray& payload) const override {
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            return {};
        }

        const QJsonObject root = document.object();
        const QString outputText = root.value(QStringLiteral("output_text")).toString();
        if (!outputText.isEmpty()) {
            return outputText;
        }

        const QJsonArray output = root.value(QStringLiteral("output")).toArray();
        for (const auto& item : output) {
            const QJsonObject object = item.toObject();
            const QString text = textFromValue(object.value(QStringLiteral("content")));
            if (!text.isEmpty()) {
                return text;
            }
        }

        return {};
    }
};

class GeminiProvider final : public ais::ai::IProvider {
public:
    [[nodiscard]] ais::ai::RequestSpec buildRequest(const ProviderProfile& profile,
                                                    const QList<ChatMessage>& messages) const override {
        QJsonArray contents;
        for (const auto& message : messages) {
            const QJsonArray parts = buildGeminiParts(message);
            if (parts.isEmpty()) {
                continue;
            }

            contents.append(QJsonObject{
                {QStringLiteral("role"), geminiRole(message.role)},
                {QStringLiteral("parts"), parts},
            });
        }

        QUrl url = appendPath(profile.baseUrl,
                              QStringLiteral("models/%1:generateContent").arg(profile.model));
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("key"), profile.apiKey);
        url.setQuery(query);

        return ais::ai::RequestSpec{
            .url = std::move(url),
            .headers =
                {
                    {QStringLiteral("Content-Type"), QStringLiteral("application/json")},
                },
            .body = QJsonDocument(QJsonObject{
                                      {QStringLiteral("contents"), contents},
                                  })
                        .toJson(QJsonDocument::Compact),
        };
    }

    [[nodiscard]] QString parseTextResponse(const QByteArray& payload) const override {
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            return {};
        }

        const QJsonArray candidates = document.object().value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) {
            return {};
        }

        return textFromValue(candidates.at(0).toObject().value(QStringLiteral("content")));
    }
};

class ClaudeProvider final : public ais::ai::IProvider {
public:
    [[nodiscard]] ais::ai::RequestSpec buildRequest(const ProviderProfile& profile,
                                                    const QList<ChatMessage>& messages) const override {
        QJsonArray requestMessages;
        for (const auto& message : messages) {
            const QJsonArray content = buildClaudeContent(message);
            if (content.isEmpty()) {
                continue;
            }

            requestMessages.append(QJsonObject{
                {QStringLiteral("role"), messageRole(message.role)},
                {QStringLiteral("content"), content},
            });
        }

        return ais::ai::RequestSpec{
            .url = appendPath(profile.baseUrl, u"messages"),
            .headers =
                {
                    {QStringLiteral("x-api-key"), profile.apiKey},
                    {QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01")},
                    {QStringLiteral("Content-Type"), QStringLiteral("application/json")},
                },
            .body = QJsonDocument(QJsonObject{
                                      {QStringLiteral("model"), profile.model},
                                      {QStringLiteral("max_tokens"), 1024},
                                      {QStringLiteral("messages"), requestMessages},
                                  })
                        .toJson(QJsonDocument::Compact),
        };
    }

    [[nodiscard]] QString parseTextResponse(const QByteArray& payload) const override {
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            return {};
        }

        return textFromValue(document.object().value(QStringLiteral("content")));
    }
};

}  // namespace

namespace ais::ai {

std::unique_ptr<IProvider> makeProvider(const ProviderProtocol protocol) {
    switch (protocol) {
    case ProviderProtocol::OpenAiChat:
    case ProviderProtocol::OpenAiCompatible:
        return std::make_unique<OpenAiChatProvider>();
    case ProviderProtocol::OpenAiResponses:
        return std::make_unique<OpenAiResponsesProvider>();
    case ProviderProtocol::Gemini:
        return std::make_unique<GeminiProvider>();
    case ProviderProtocol::Claude:
        return std::make_unique<ClaudeProvider>();
    }

    return {};
}

}  // namespace ais::ai
