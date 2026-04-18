#include "ai/provider_test_runner.h"

#include <algorithm>
#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include "ai/provider_factory.h"
#include "ai/sample_image_factory.h"
#include "chat/chat_message.h"

namespace ais::ai {

namespace {

using ais::config::ProviderProfile;
using ais::config::ProviderProtocol;

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

[[nodiscard]] RequestSpec buildModelListRequest(const ProviderProfile& profile) {
    RequestSpec spec;

    switch (profile.protocol) {
    case ProviderProtocol::OpenAiChat:
    case ProviderProtocol::OpenAiResponses:
    case ProviderProtocol::OpenAiCompatible:
        spec.url = appendPath(profile.baseUrl, u"models");
        spec.headers.insert(QStringLiteral("Authorization"),
                            QStringLiteral("Bearer %1").arg(profile.apiKey));
        break;
    case ProviderProtocol::Gemini: {
        spec.url = appendPath(profile.baseUrl, u"models");
        QUrlQuery query(spec.url);
        query.addQueryItem(QStringLiteral("key"), profile.apiKey);
        spec.url.setQuery(query);
        break;
    }
    case ProviderProtocol::Claude:
        spec.url = appendPath(profile.baseUrl, u"models");
        spec.headers.insert(QStringLiteral("x-api-key"), profile.apiKey);
        spec.headers.insert(QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01"));
        break;
    }

    return spec;
}

void appendUniqueModel(QStringList* models, QSet<QString>* seen, QString model) {
    model = model.trimmed();
    if (model.startsWith(QStringLiteral("models/"))) {
        model.remove(0, QStringLiteral("models/").size());
    }
    if (model.isEmpty() || seen == nullptr || models == nullptr || seen->contains(model)) {
        return;
    }
    seen->insert(model);
    models->append(model);
}

[[nodiscard]] bool geminiSupportsGenerateContent(const QJsonObject& object) {
    const QJsonArray methods = object.value(QStringLiteral("supportedGenerationMethods")).toArray();
    if (methods.isEmpty()) {
        return true;
    }

    for (const QJsonValue& value : methods) {
        if (value.toString() == QStringLiteral("generateContent")) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] QStringList sortedModels(QStringList models) {
    std::sort(models.begin(), models.end(),
              [](const QString& left, const QString& right) {
                  return QString::compare(left, right, Qt::CaseInsensitive) < 0;
              });
    return models;
}

[[nodiscard]] QStringList parseModelListResponse(const ProviderProtocol protocol,
                                                 const QByteArray& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return {};
    }

    const QJsonObject root = document.object();
    QStringList models;
    QSet<QString> seen;

    if (protocol == ProviderProtocol::Gemini) {
        for (const QJsonValue& value : root.value(QStringLiteral("models")).toArray()) {
            const QJsonObject object = value.toObject();
            if (!geminiSupportsGenerateContent(object)) {
                continue;
            }

            const QString preferredId = object.value(QStringLiteral("baseModelId")).toString();
            const QString fallbackId = object.value(QStringLiteral("name")).toString();
            appendUniqueModel(&models, &seen, preferredId.isEmpty() ? fallbackId : preferredId);
        }
        return sortedModels(models);
    }

    auto appendFromArray = [&](const QJsonArray& array) {
        for (const QJsonValue& value : array) {
            const QJsonObject object = value.toObject();
            appendUniqueModel(&models, &seen, object.value(QStringLiteral("id")).toString());
            appendUniqueModel(&models, &seen, object.value(QStringLiteral("name")).toString());
        }
    };

    appendFromArray(root.value(QStringLiteral("data")).toArray());
    appendFromArray(root.value(QStringLiteral("models")).toArray());
    appendFromArray(root.value(QStringLiteral("items")).toArray());

    return sortedModels(models);
}

}  // namespace

ProviderTestRunner::ProviderTestRunner(std::unique_ptr<INetworkTransport> transport, app::RequestGuard& guard)
    : transport_(std::move(transport)),
      guard_(guard) {}

ProviderTestRunner::~ProviderTestRunner() = default;

bool ProviderTestRunner::runTextTest(const config::ProviderProfile& profile,
                                     SuccessHandler onSuccess,
                                     FailureHandler onFailure) {
    return runMessages(profile,
                       {
                           chat::ChatMessage{
                               .role = chat::ChatRole::User,
                               .text = QStringLiteral("Reply with OK only"),
                           },
                       },
                       std::move(onSuccess),
                       std::move(onFailure));
}

bool ProviderTestRunner::runImageTest(const config::ProviderProfile& profile,
                                      SuccessHandler onSuccess,
                                      FailureHandler onFailure) {
    return runMessages(profile,
                       {
                           chat::ChatMessage{
                               .role = chat::ChatRole::User,
                               .imageBytes = SampleImageFactory::buildPng(),
                           },
                           chat::ChatMessage{
                               .role = chat::ChatRole::User,
                               .text = QStringLiteral("Reply with OK only"),
                           },
                       },
                       std::move(onSuccess),
                       std::move(onFailure));
}

bool ProviderTestRunner::fetchModels(const config::ProviderProfile& profile,
                                     ModelsHandler onSuccess,
                                     FailureHandler onFailure) {
    if (!transport_) {
        if (onFailure) {
            onFailure(QStringLiteral("Provider test transport is not configured"));
        }
        return false;
    }

    if (!guard_.tryEnter(app::BusyState::TestingProvider)) {
        return false;
    }

    const RequestSpec spec = buildModelListRequest(profile);
    app::RequestGuard* const guard = &guard_;
    const ProviderProtocol protocol = profile.protocol;
    transport_->get(
        spec,
        [guard, protocol, onSuccess = std::move(onSuccess)](QByteArray payload) mutable {
            const auto release = qScopeGuard([guard] { guard->leave(app::BusyState::TestingProvider); });
            if (onSuccess) {
                onSuccess(parseModelListResponse(protocol, payload));
            }
        },
        [guard, onFailure = std::move(onFailure)](QString error) mutable {
            const auto release = qScopeGuard([guard] { guard->leave(app::BusyState::TestingProvider); });
            if (onFailure) {
                onFailure(std::move(error));
            }
        });

    return true;
}

bool ProviderTestRunner::runMessages(const config::ProviderProfile& profile,
                                     const QList<chat::ChatMessage>& messages,
                                     SuccessHandler onSuccess,
                                     FailureHandler onFailure) {
    if (!transport_) {
        if (onFailure) {
            onFailure(QStringLiteral("Provider test transport is not configured"));
        }
        return false;
    }

    if (!guard_.tryEnter(app::BusyState::TestingProvider)) {
        return false;
    }

    auto providerHandle = makeProvider(profile.protocol);
    if (!providerHandle) {
        guard_.leave(app::BusyState::TestingProvider);
        if (onFailure) {
            onFailure(QStringLiteral("Unsupported provider protocol"));
        }
        return false;
    }

    const auto provider = std::shared_ptr<IProvider>(providerHandle.release());
    const RequestSpec spec = provider->buildRequest(profile, messages);
    app::RequestGuard* const guard = &guard_;
    transport_->post(
        spec,
        [guard, provider, onSuccess = std::move(onSuccess)](QByteArray payload) mutable {
            const auto release = qScopeGuard([guard] { guard->leave(app::BusyState::TestingProvider); });
            const QString text = provider->parseTextResponse(payload);
            if (onSuccess) {
                onSuccess(text);
            }
        },
        [guard, onFailure = std::move(onFailure)](QString error) mutable {
            const auto release = qScopeGuard([guard] { guard->leave(app::BusyState::TestingProvider); });
            if (onFailure) {
                onFailure(std::move(error));
            }
        });

    return true;
}

}  // namespace ais::ai
