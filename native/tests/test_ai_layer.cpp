#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <utility>

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include "ai/ai_client.h"
#include "ai/provider_factory.h"
#include "ai/provider_test_runner.h"
#include "app/request_guard.h"
#include "chat/chat_session.h"
#include "config/provider_profile.h"

namespace {

using ais::ai::AiClient;
using ais::ai::INetworkTransport;
using ais::ai::ProviderTestRunner;
using ais::ai::RequestSpec;
using ais::ai::makeProvider;
using ais::app::BusyState;
using ais::app::RequestGuard;
using ais::chat::ChatSession;
using ais::config::ProviderProfile;
using ais::config::ProviderProtocol;

struct TransportProbe {
    RequestSpec lastSpec;
    std::function<void(QByteArray)> success;
    std::function<void(QByteArray)> chunk;
    std::function<void()> complete;
    std::function<void(QString)> failure;
    int cancelCount = 0;
};

class FakeTransport final : public INetworkTransport {
public:
    explicit FakeTransport(std::shared_ptr<TransportProbe> probe)
        : probe_(std::move(probe)) {}

    void get(const RequestSpec& spec,
             std::function<void(QByteArray)> onSuccess,
             std::function<void(QString)> onFailure) override {
        probe_->lastSpec = spec;
        probe_->success = std::move(onSuccess);
        probe_->failure = std::move(onFailure);
    }

    void post(const RequestSpec& spec,
              std::function<void(QByteArray)> onSuccess,
              std::function<void(QString)> onFailure) override {
        probe_->lastSpec = spec;
        probe_->success = std::move(onSuccess);
        probe_->failure = std::move(onFailure);
    }

    void postStream(const RequestSpec& spec,
                    ChunkHandler onChunk,
                    CompletionHandler onComplete,
                    FailureHandler onFailure) override {
        probe_->lastSpec = spec;
        probe_->chunk = std::move(onChunk);
        probe_->complete = std::move(onComplete);
        probe_->failure = std::move(onFailure);
    }

    void cancelActiveRequest() override {
        probe_->cancelCount += 1;
    }

private:
    std::shared_ptr<TransportProbe> probe_;
};

[[nodiscard]] ProviderProfile makeProfile(ProviderProtocol protocol,
                                          QString baseUrl,
                                          QString apiKey,
                                          QString model) {
    return ProviderProfile{
        .protocol = protocol,
        .baseUrl = std::move(baseUrl),
        .apiKey = std::move(apiKey),
        .model = std::move(model),
    };
}

[[nodiscard]] QJsonDocument parseJson(const QByteArray& payload) {
    return QJsonDocument::fromJson(payload);
}

[[nodiscard]] QByteArray noisyPngBytes(const QSize& size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int value = (x * 37 + y * 61 + (x ^ y) * 17) & 0xff;
            row[x] = qRgba(value, (value * 53) & 0xff, (value * 97) & 0xff, 255);
        }
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

template <typename Type>
struct AlignedStorage {
    alignas(Type) std::array<std::byte, sizeof(Type)> bytes{};

    [[nodiscard]] std::byte* data() noexcept { return bytes.data(); }
    [[nodiscard]] const std::byte* data() const noexcept { return bytes.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return bytes.size(); }
};

template <typename Type, typename... Args>
[[nodiscard]] Type* constructInStorage(AlignedStorage<Type>& storage, Args&&... args) {
    return new (storage.data()) Type(std::forward<Args>(args)...);
}

static_assert(alignof(AlignedStorage<AiClient>) >= alignof(AiClient));
static_assert(alignof(AlignedStorage<ProviderTestRunner>) >= alignof(ProviderTestRunner));

}  // namespace

class AiLayerTests final : public QObject {
    Q_OBJECT

private slots:
    void openAiChatBuildsImageUrlPayload();
    void openAiCompatibleBuildsChatCompletionsPayload();
    void openAiCompatibleGrokImagePayloadUsesHighDetail();
    void responsesBuildsInputImagePayload();
    void responsesImageRequestForGrokProxyDisablesStore();
    void responsesImageRequestForGrokProxyUsesHighDetail();
    void responsesLargeGrokImagePayloadPrefersJpegDataUri();
    void geminiBuildsGenerateContentRequest();
    void claudeBuildsBase64ImagePayload();
    void sendConversationLocksUntilCallbackReturns();
    void sendConversationReleasesBusyStateAfterClientDestructionOnSuccess();
    void failedRequestReleasesBusyStateAfterClientDestruction();
    void failedRequestReleasesBusyState();
    void responsesImageConversationStreamsThroughResponses();
    void responsesFollowUpAfterImageReplyKeepsHistoricalImage();
    void responsesStreamingEmitsTextAndReasoningDeltas();
    void responsesStreamingAcceptsStructuredOutputTextDelta();
    void responsesStreamingUsesOutputTextDoneWhenNoDeltaArrives();
    void responsesStreamingUsesContentPartDoneWhenNoDeltaArrives();
    void responsesStreamingUsesOutputItemDoneWhenNoDeltaArrives();
    void responsesStreamingFallsBackToCompletedEventPayload();
    void responsesStreamingFallsBackToDoneEventPayload();
    void responsesStreamingFallsBackToChatCompletionsChunksOnResponsesProtocol();
    void responsesStreamingTreatsUpstreamErrorEventAsFailure();
    void chatStreamingTreatsSseErrorObjectAsFailure();
    void responsesImageRetryFallsBackToNonStreamingRequest();
    void streamingCompletionCallbackRunsAfterBusyStateReleased();
    void streamingFailureCallbackRunsAfterBusyStateReleased();
    void cancelActiveStreamReleasesBusyStateAndSuppressesCallbacks();
    void streamingRequestIncludesNoCacheRetryHeaders();
    void providerTextTestLocksSendsPromptAndReleasesOnSuccess();
    void providerImageTestIncludesSamplePngAndReleasesOnSuccess();
    void providerImageTestForResponsesUsesResponsesEndpoint();
    void providerImageTestReleasesBusyStateOnFailure();
    void fetchModelsUsesOpenAiModelsEndpointAndParsesIds();
    void fetchModelsForGeminiFiltersToGenerateContentModels();
};

void AiLayerTests::openAiChatBuildsImageUrlPayload() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiChat,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("openai-key"),
        QStringLiteral("gpt-test"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    const QJsonArray messages = root.value("messages").toArray();
    const QJsonObject firstMessage = messages.at(0).toObject();
    const QJsonArray firstContent = firstMessage.value("content").toArray();
    const QJsonObject imageEntry = firstContent.at(0).toObject();

    QCOMPARE(spec.url.toString(), QStringLiteral("https://api.openai.com/v1/chat/completions"));
    QCOMPARE(spec.headers.value("Authorization"), QStringLiteral("Bearer openai-key"));
    QCOMPARE(root.value("model").toString(), QStringLiteral("gpt-test"));
    QCOMPARE(root.value("stream").toBool(), false);
    QCOMPARE(firstMessage.value("role").toString(), QStringLiteral("user"));
    QCOMPARE(imageEntry.value("type").toString(), QStringLiteral("image_url"));
    QCOMPARE(imageEntry.value("image_url").toObject().value("url").toString(),
             QStringLiteral("data:image/png;base64,cG5nLWltYWdl"));
}

void AiLayerTests::openAiCompatibleBuildsChatCompletionsPayload() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiCompatible,
        QStringLiteral("https://compat.example.test/v1"),
        QStringLiteral("compat-key"),
        QStringLiteral("compat-model"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    const QJsonArray messages = root.value("messages").toArray();
    const QJsonObject firstMessage = messages.at(0).toObject();
    const QJsonArray firstContent = firstMessage.value("content").toArray();
    const QJsonArray secondContent = messages.at(1).toObject().value("content").toArray();

    QCOMPARE(spec.url.toString(), QStringLiteral("https://compat.example.test/v1/chat/completions"));
    QCOMPARE(spec.headers.value("Authorization"), QStringLiteral("Bearer compat-key"));
    QCOMPARE(root.value("model").toString(), QStringLiteral("compat-model"));
    QCOMPARE(root.value("stream").toBool(), false);
    QCOMPARE(firstContent.at(0).toObject().value("type").toString(), QStringLiteral("image_url"));
    QCOMPARE(secondContent.at(0).toObject().value("type").toString(), QStringLiteral("text"));
    QCOMPARE(secondContent.at(0).toObject().value("text").toString(), QStringLiteral("Describe it"));
}

void AiLayerTests::openAiCompatibleGrokImagePayloadUsesHighDetail() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiCompatible,
        QStringLiteral("https://520.wcgit.com/v1"),
        QStringLiteral("compat-key"),
        QStringLiteral("grok-4.20-0309"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonArray messages = document.object().value(QStringLiteral("messages")).toArray();
    const QJsonArray content = messages.at(0).toObject().value(QStringLiteral("content")).toArray();
    const QJsonObject imageUrl = content.at(0).toObject().value(QStringLiteral("image_url")).toObject();

    QCOMPARE(imageUrl.value(QStringLiteral("detail")).toString(), QStringLiteral("high"));
}

void AiLayerTests::responsesBuildsInputImagePayload() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    const QJsonArray input = root.value("input").toArray();
    QCOMPARE(input.size(), 1);
    const QJsonObject firstMessage = input.at(0).toObject();
    const QJsonArray firstContent = firstMessage.value("content").toArray();
    const QJsonObject imageEntry = firstContent.at(0).toObject();
    const QJsonObject textEntry = firstContent.at(1).toObject();

    QCOMPARE(spec.url.toString(), QStringLiteral("https://api.openai.com/v1/responses"));
    QCOMPARE(spec.headers.value("Authorization"), QStringLiteral("Bearer responses-key"));
    QCOMPARE(root.value("model").toString(), QStringLiteral("gpt-responses"));
    QCOMPARE(root.value("stream").toBool(), false);
    QCOMPARE(imageEntry.value("type").toString(), QStringLiteral("input_image"));
    QCOMPARE(imageEntry.value("image_url").toString(), QStringLiteral("data:image/png;base64,cG5nLWltYWdl"));
    QCOMPARE(textEntry.value("type").toString(), QStringLiteral("input_text"));
    QCOMPARE(textEntry.value("text").toString(), QStringLiteral("Describe it"));
}

void AiLayerTests::responsesImageRequestForGrokProxyDisablesStore() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://520.wcgit.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("grok-4.20-0309"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();

    QVERIFY(root.contains(QStringLiteral("store")));
    QVERIFY(!root.value(QStringLiteral("store")).toBool(true));
}

void AiLayerTests::responsesImageRequestForGrokProxyUsesHighDetail() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://520.wcgit.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("grok-4.20-0309"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonArray input = document.object().value(QStringLiteral("input")).toArray();
    QCOMPARE(input.size(), 1);
    const QJsonArray content = input.at(0).toObject().value(QStringLiteral("content")).toArray();
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("input_image"));
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("detail")).toString(),
             QStringLiteral("high"));
}

void AiLayerTests::responsesLargeGrokImagePayloadPrefersJpegDataUri() {
    ChatSession session;
    const QByteArray largePng = noisyPngBytes(QSize(2600, 2200));
    session.beginWithCapture(largePng);
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://520.wcgit.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("grok-4.20-0309"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonArray input = document.object().value(QStringLiteral("input")).toArray();
    const QJsonArray content = input.at(0).toObject().value(QStringLiteral("content")).toArray();
    const QString imageUrl = content.at(0).toObject().value(QStringLiteral("image_url")).toString();
    const QString originalDataUri = QStringLiteral("data:image/png;base64,%1")
        .arg(QString::fromLatin1(largePng.toBase64()));

    QVERIFY(imageUrl.startsWith(QStringLiteral("data:image/")));
    QVERIFY(imageUrl != originalDataUri);
}

void AiLayerTests::geminiBuildsGenerateContentRequest() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::Gemini,
        QStringLiteral("https://generativelanguage.googleapis.com/v1beta"),
        QStringLiteral("gem-key"),
        QStringLiteral("gemini-2.5-flash"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    const QJsonArray contents = root.value("contents").toArray();
    const QJsonObject firstMessage = contents.at(0).toObject();
    const QJsonArray firstParts = firstMessage.value("parts").toArray();
    const QJsonObject inlineData = firstParts.at(0).toObject().value("inline_data").toObject();

    QCOMPARE(spec.url.toString(),
             QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=gem-key"));
    QVERIFY(!spec.headers.contains("Authorization"));
    QCOMPARE(firstMessage.value("role").toString(), QStringLiteral("user"));
    QCOMPARE(inlineData.value("mime_type").toString(), QStringLiteral("image/png"));
    QCOMPARE(inlineData.value("data").toString(), QStringLiteral("cG5nLWltYWdl"));
}

void AiLayerTests::claudeBuildsBase64ImagePayload() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::Claude,
        QStringLiteral("https://api.anthropic.com/v1"),
        QStringLiteral("claude-key"),
        QStringLiteral("claude-sonnet-4-0"));

    const auto provider = makeProvider(profile.protocol);
    QVERIFY(provider != nullptr);

    const RequestSpec spec = provider->buildRequest(profile, session.messages());
    const QJsonDocument document = parseJson(spec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    const QJsonArray messages = root.value("messages").toArray();
    const QJsonObject firstMessage = messages.at(0).toObject();
    const QJsonArray firstContent = firstMessage.value("content").toArray();
    const QJsonObject imageEntry = firstContent.at(0).toObject();
    const QJsonObject source = imageEntry.value("source").toObject();

    QCOMPARE(spec.url.toString(), QStringLiteral("https://api.anthropic.com/v1/messages"));
    QCOMPARE(spec.headers.value("x-api-key"), QStringLiteral("claude-key"));
    QCOMPARE(root.value("model").toString(), QStringLiteral("claude-sonnet-4-0"));
    QCOMPARE(imageEntry.value("type").toString(), QStringLiteral("image"));
    QCOMPARE(source.value("type").toString(), QStringLiteral("base64"));
    QCOMPARE(source.value("media_type").toString(), QStringLiteral("image/png"));
    QCOMPARE(source.value("data").toString(), QStringLiteral("cG5nLWltYWdl"));
}

void AiLayerTests::sendConversationLocksUntilCallbackReturns() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiChat,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("openai-key"),
        QStringLiteral("gpt-test"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    auto transport = std::make_unique<FakeTransport>(probe);
    AiClient client(std::move(transport), guard);

    bool successCalled = false;
    bool callbackSawBusyState = false;

    QVERIFY(client.sendConversation(
        profile,
        session.messages(),
        [&](const QString& text) {
            successCalled = true;
            callbackSawBusyState = guard.state() == BusyState::RequestInFlight;
            QCOMPARE(text, QStringLiteral("All set"));
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));

    probe->success(QByteArray(R"({"choices":[{"message":{"content":"All set"}}]})"));

    QVERIFY(successCalled);
    QVERIFY(callbackSawBusyState);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::sendConversationReleasesBusyStateAfterClientDestructionOnSuccess() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiChat,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("openai-key"),
        QStringLiteral("gpt-test"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AlignedStorage<AiClient> storage{};
    AiClient* client = constructInStorage<AiClient>(
        storage,
        std::make_unique<FakeTransport>(probe),
        guard);

    bool successCalled = false;
    QVERIFY(client->sendConversation(
        profile,
        session.messages(),
        [&](const QString& text) {
            successCalled = true;
            QCOMPARE(text, QStringLiteral("Still safe"));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));

    client->~AiClient();
    std::memset(storage.data(), 0, storage.size());

    probe->success(QByteArray(R"({"choices":[{"message":{"content":"Still safe"}}]})"));

    QVERIFY(successCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::failedRequestReleasesBusyStateAfterClientDestruction() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AlignedStorage<AiClient> storage{};
    AiClient* client = constructInStorage<AiClient>(
        storage,
        std::make_unique<FakeTransport>(probe),
        guard);

    bool failureCalled = false;
    QVERIFY(client->sendConversation(
        profile,
        session.messages(),
        [&](const QString& text) {
            QFAIL(qPrintable(QStringLiteral("unexpected success: %1").arg(text)));
        },
        [&](const QString& error) {
            failureCalled = true;
            QCOMPARE(error, QStringLiteral("network down"));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));

    client->~AiClient();
    std::memset(storage.data(), 0, storage.size());

    probe->failure(QStringLiteral("network down"));

    QVERIFY(failureCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::failedRequestReleasesBusyState() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    auto transport = std::make_unique<FakeTransport>(probe);
    AiClient client(std::move(transport), guard);

    bool failureCalled = false;
    bool callbackSawBusyState = false;

    QVERIFY(client.sendConversation(
        profile,
        session.messages(),
        [&](const QString& text) {
            QFAIL(qPrintable(QStringLiteral("unexpected success: %1").arg(text)));
        },
        [&](const QString& error) {
            failureCalled = true;
            callbackSawBusyState = guard.state() == BusyState::RequestInFlight;
            QCOMPARE(error, QStringLiteral("network down"));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));

    probe->failure(QStringLiteral("network down"));

    QVERIFY(failureCalled);
    QVERIFY(callbackSawBusyState);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesImageConversationStreamsThroughResponses() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [](const QString&) {},
        [](const QString&) {},
        []() {},
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(probe->lastSpec.url.toString(), QStringLiteral("https://api.example.test/v1/responses"));
    const QJsonDocument document = parseJson(probe->lastSpec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("stream")).toBool(), true);

    const QJsonArray input = root.value(QStringLiteral("input")).toArray();
    QCOMPARE(input.size(), 1);
    const QJsonArray firstContent = input.at(0).toObject().value(QStringLiteral("content")).toArray();
    QCOMPARE(firstContent.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("input_image"));
    QCOMPARE(firstContent.at(0).toObject().value(QStringLiteral("image_url")).toString(),
             QStringLiteral("data:image/png;base64,cG5nLWltYWdl"));
    QCOMPARE(firstContent.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("input_text"));
    QCOMPARE(firstContent.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Describe it"));
}

void AiLayerTests::responsesFollowUpAfterImageReplyKeepsHistoricalImage() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Describe it"));
    session.addAssistantText(QStringLiteral("It shows a window"));
    session.addUserText(QStringLiteral("Summarize the key warning"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [](const QString&) {},
        [](const QString&) {},
        []() {},
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(probe->lastSpec.url.toString(), QStringLiteral("https://api.example.test/v1/responses"));
    const QJsonDocument document = parseJson(probe->lastSpec.body);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("stream")).toBool(), true);

    const QJsonArray input = root.value(QStringLiteral("input")).toArray();
    QCOMPARE(input.size(), 3);
    QCOMPARE(input.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(input.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
    QCOMPARE(input.at(2).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));

    const QJsonArray imageContent = input.at(0).toObject().value(QStringLiteral("content")).toArray();
    QCOMPARE(imageContent.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("input_image"));
    QCOMPARE(imageContent.at(0).toObject().value(QStringLiteral("image_url")).toString(),
             QStringLiteral("data:image/png;base64,cG5nLWltYWdl"));

    QCOMPARE(imageContent.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("input_text"));
    QCOMPARE(imageContent.at(1).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("Describe it"));
    QCOMPARE(input.at(1).toObject().value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("It shows a window"));
    QCOMPARE(input.at(2).toObject().value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("Summarize the key warning"));
}

void AiLayerTests::responsesStreamingEmitsTextAndReasoningDeltas() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString reasoning;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [&](const QString& delta) { reasoning += delta; },
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));

    probe->chunk(QByteArray(
        "event: response.reasoning_text.delta\n"
        "data: {\"type\":\"response.reasoning_text.delta\",\"delta\":\"Thinking\"}\n\n"
        "event: response.output_text.delta\n"
        "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Answer\"}\n\n"));
    probe->complete();

    QCOMPARE(reasoning, QStringLiteral("Thinking"));
    QCOMPARE(text, QStringLiteral("Answer"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingAcceptsStructuredOutputTextDelta() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        {},
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: response.output_text.delta\n"
        "data: {\"type\":\"response.output_text.delta\",\"delta\":{\"text\":\"Answer\"}}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingUsesOutputTextDoneWhenNoDeltaArrives() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        {},
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: response.output_text.done\n"
        "data: {\"type\":\"response.output_text.done\",\"text\":\"Answer\"}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingUsesContentPartDoneWhenNoDeltaArrives() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        {},
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: response.content_part.done\n"
        "data: {\"type\":\"response.content_part.done\",\"part\":{\"type\":\"output_text\",\"text\":\"Answer\"}}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingUsesOutputItemDoneWhenNoDeltaArrives() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString reasoning;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [&](const QString& delta) { reasoning += delta; },
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: response.output_item.done\n"
        "data: {\"type\":\"response.output_item.done\",\"item\":{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Answer\"},{\"type\":\"summary_text\",\"text\":\"Thinking\"}]}}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QCOMPARE(reasoning, QStringLiteral("Thinking"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingFallsBackToCompletedEventPayload() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString reasoning;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [&](const QString& delta) { reasoning += delta; },
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: response.completed\n"
        "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_123\",\"output_text\":\"Answer\","
        "\"output\":[{\"type\":\"reasoning\",\"summary\":[{\"type\":\"summary_text\",\"text\":\"Thinking\"}]},"
        "{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Answer\"}]}]}}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QCOMPARE(reasoning, QStringLiteral("Thinking"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingFallsBackToDoneEventPayload() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString reasoning;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [&](const QString& delta) { reasoning += delta; },
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: response.done\n"
        "data: {\"type\":\"response.done\",\"response\":{\"id\":\"resp_456\",\"output_text\":\"Answer\","
        "\"output\":[{\"type\":\"reasoning\",\"summary\":[{\"type\":\"summary_text\",\"text\":\"Thinking\"}]},"
        "{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Answer\"}]}]}}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QCOMPARE(reasoning, QStringLiteral("Thinking"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingFallsBackToChatCompletionsChunksOnResponsesProtocol() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString reasoning;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [&](const QString& delta) { reasoning += delta; },
        [&]() { completed = true; },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "data: {\"choices\":[{\"delta\":{\"reasoning_text\":\"Thinking\",\"content\":\"Answer\"}}]}\n\n"));
    probe->complete();

    QCOMPARE(text, QStringLiteral("Answer"));
    QCOMPARE(reasoning, QStringLiteral("Thinking"));
    QVERIFY(completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesStreamingTreatsUpstreamErrorEventAsFailure() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("grok-4.20-0309"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString failure;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [](const QString&) {},
        [&]() { completed = true; },
        [&](const QString& error) { failure = error; }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "event: upstream_error\n"
        "data: {\"type\":\"upstream_error\",\"message\":\"Asset upload returned 400\",\"code\":\"upstream_error\"}\n\n"));
    probe->complete();

    QCOMPARE(text, QString());
    QCOMPARE(failure, QStringLiteral("Asset upload returned 400"));
    QVERIFY(!completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::chatStreamingTreatsSseErrorObjectAsFailure() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiCompatible,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("compat-key"),
        QStringLiteral("grok-4.20-0309"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QString text;
    QString failure;
    bool completed = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString& delta) { text += delta; },
        [](const QString&) {},
        [&]() { completed = true; },
        [&](const QString& error) { failure = error; }));

    QVERIFY(probe->chunk);
    QVERIFY(probe->complete);

    probe->chunk(QByteArray(
        "data: {\"error\":{\"message\":\"Asset upload returned 400\",\"type\":\"upstream_error\",\"code\":\"upstream_error\"}}\n\n"
        "data: {\"id\":\"\",\"object\":\"chat.completion.chunk\",\"choices\":[],\"usage\":{\"input_tokens\":0}}\n\n"));
    probe->complete();

    QCOMPARE(text, QString());
    QCOMPARE(failure, QStringLiteral("Asset upload returned 400"));
    QVERIFY(!completed);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::responsesImageRetryFallsBackToNonStreamingRequest() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("grok-4.20-0309"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [](const QString&) {},
        [](const QString&) {},
        []() {},
        [](const QString&) {},
        1));

    QVERIFY(static_cast<bool>(probe->success));
    QVERIFY(!static_cast<bool>(probe->chunk));

    const QJsonDocument document = parseJson(probe->lastSpec.body);
    QVERIFY(document.isObject());
    QVERIFY(!document.object().value(QStringLiteral("stream")).toBool(true));

    client.cancelActiveRequest();
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::streamingCompletionCallbackRunsAfterBusyStateReleased() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    bool completed = false;
    bool completionSawIdle = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [](const QString&) {},
        [](const QString&) {},
        [&]() {
            completed = true;
            completionSawIdle = guard.state() == BusyState::Idle;
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));
    QVERIFY(probe->complete);

    probe->complete();

    QVERIFY(completed);
    QVERIFY(completionSawIdle);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::streamingFailureCallbackRunsAfterBusyStateReleased() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    bool failureCalled = false;
    bool failureSawIdle = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [](const QString&) {},
        [](const QString&) {},
        []() {
            QFAIL("unexpected completion");
        },
        [&](const QString& error) {
            failureCalled = true;
            failureSawIdle = guard.state() == BusyState::Idle;
            QCOMPARE(error, QStringLiteral("network down"));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));
    QVERIFY(probe->failure);

    probe->failure(QStringLiteral("network down"));

    QVERIFY(failureCalled);
    QVERIFY(failureSawIdle);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::cancelActiveStreamReleasesBusyStateAndSuppressesCallbacks() {
    ChatSession session;
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    bool textCalled = false;
    bool reasoningCalled = false;
    bool completionCalled = false;
    bool failureCalled = false;

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [&](const QString&) { textCalled = true; },
        [&](const QString&) { reasoningCalled = true; },
        [&]() { completionCalled = true; },
        [&](const QString&) { failureCalled = true; }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::RequestInFlight));

    client.cancelActiveRequest();

    QCOMPARE(probe->cancelCount, 1);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));

    probe->chunk(QByteArray(
        "event: response.output_text.delta\n"
        "data: {\"type\":\"response.output_text.delta\",\"delta\":\"ignored\"}\n\n"));
    probe->complete();
    probe->failure(QStringLiteral("cancelled"));

    QVERIFY(!textCalled);
    QVERIFY(!reasoningCalled);
    QVERIFY(!completionCalled);
    QVERIFY(!failureCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::streamingRequestIncludesNoCacheRetryHeaders() {
    ChatSession session;
    session.beginWithCapture(QByteArray("png-image"));
    session.addUserText(QStringLiteral("Explain"));

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AiClient client(std::make_unique<FakeTransport>(probe), guard);

    QVERIFY(client.sendConversationStream(
        profile,
        session.messages(),
        [](const QString&) {},
        [](const QString&) {},
        []() {},
        [](const QString&) {},
        2));

    QCOMPARE(probe->lastSpec.headers.value(QStringLiteral("Cache-Control")), QStringLiteral("no-cache"));
    QCOMPARE(probe->lastSpec.headers.value(QStringLiteral("Pragma")), QStringLiteral("no-cache"));
    QCOMPARE(probe->lastSpec.headers.value(QStringLiteral("X-ASnap-Retry-Attempt")), QStringLiteral("2"));
    QVERIFY(!probe->lastSpec.headers.value(QStringLiteral("X-ASnap-Request-Id")).trimmed().isEmpty());

    client.cancelActiveRequest();
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::providerTextTestLocksSendsPromptAndReleasesOnSuccess() {
    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AlignedStorage<ProviderTestRunner> storage{};
    ProviderTestRunner* runner = constructInStorage<ProviderTestRunner>(
        storage,
        std::make_unique<FakeTransport>(probe),
        guard);

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiChat,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("openai-key"),
        QStringLiteral("gpt-test"));

    bool successCalled = false;
    QVERIFY(runner->runTextTest(
        profile,
        [&](const QString& text) {
            successCalled = true;
            QCOMPARE(text, QStringLiteral("OK"));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));

    const QJsonDocument document = parseJson(probe->lastSpec.body);
    QVERIFY(document.isObject());
    const QJsonArray messages = document.object().value("messages").toArray();
    const QJsonArray content = messages.at(0).toObject().value("content").toArray();
    QCOMPARE(content.at(0).toObject().value("text").toString(), QStringLiteral("Reply with OK only"));

    runner->~ProviderTestRunner();
    std::memset(storage.data(), 0, storage.size());

    probe->success(QByteArray(R"({"choices":[{"message":{"content":"OK"}}]})"));

    QVERIFY(successCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::providerImageTestIncludesSamplePngAndReleasesOnSuccess() {
    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    ProviderTestRunner runner(std::make_unique<FakeTransport>(probe), guard);

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiChat,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("openai-key"),
        QStringLiteral("gpt-test"));

    bool successCalled = false;
    QVERIFY(runner.runImageTest(
        profile,
        [&](const QString& text) {
            successCalled = true;
            QCOMPARE(text, QStringLiteral("OK"));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));

    const QJsonDocument document = parseJson(probe->lastSpec.body);
    QVERIFY(document.isObject());
    const QJsonArray messages = document.object().value("messages").toArray();
    const QJsonArray imageContent = messages.at(0).toObject().value("content").toArray();
    const QString imageUrl = imageContent.at(0).toObject().value("image_url").toObject().value("url").toString();
    QVERIFY(imageUrl.startsWith(QStringLiteral("data:image/png;base64,iVBORw0KGgo")));

    probe->success(QByteArray(R"({"choices":[{"message":{"content":"OK"}}]})"));

    QVERIFY(successCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::providerImageTestForResponsesUsesResponsesEndpoint() {
    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    ProviderTestRunner runner(std::make_unique<FakeTransport>(probe), guard);

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.example.test/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-responses"));

    QVERIFY(runner.runImageTest(
        profile,
        [](const QString&) {},
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(probe->lastSpec.url.toString(), QStringLiteral("https://api.example.test/v1/responses"));
    const QJsonDocument document = parseJson(probe->lastSpec.body);
    QVERIFY(document.isObject());
    const QJsonArray input = document.object().value(QStringLiteral("input")).toArray();
    QCOMPARE(input.size(), 1);
    const QJsonArray content = input.at(0).toObject().value(QStringLiteral("content")).toArray();
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("input_image"));
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("input_text"));
}

void AiLayerTests::providerImageTestReleasesBusyStateOnFailure() {
    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    AlignedStorage<ProviderTestRunner> storage{};
    ProviderTestRunner* runner = constructInStorage<ProviderTestRunner>(
        storage,
        std::make_unique<FakeTransport>(probe),
        guard);

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiChat,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("openai-key"),
        QStringLiteral("gpt-test"));

    bool failureCalled = false;
    QVERIFY(runner->runImageTest(
        profile,
        [&](const QString& text) {
            QFAIL(qPrintable(QStringLiteral("unexpected success: %1").arg(text)));
        },
        [&](const QString& error) {
            failureCalled = true;
            QCOMPARE(error, QStringLiteral("image request failed"));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));
        }));

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));

    runner->~ProviderTestRunner();
    std::memset(storage.data(), 0, storage.size());

    probe->failure(QStringLiteral("image request failed"));

    QVERIFY(failureCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::fetchModelsUsesOpenAiModelsEndpointAndParsesIds() {
    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    ProviderTestRunner runner(std::make_unique<FakeTransport>(probe), guard);

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::OpenAiResponses,
        QStringLiteral("https://api.openai.com/v1"),
        QStringLiteral("responses-key"),
        QStringLiteral("gpt-4.1-mini"));

    bool successCalled = false;
    QVERIFY(runner.fetchModels(
        profile,
        [&](const QStringList& models) {
            successCalled = true;
            QCOMPARE(models,
                     QStringList({QStringLiteral("gpt-4.1-mini"), QStringLiteral("gpt-4o")}));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(probe->lastSpec.url.toString(), QStringLiteral("https://api.openai.com/v1/models"));
    QCOMPARE(probe->lastSpec.headers.value(QStringLiteral("Authorization")),
             QStringLiteral("Bearer responses-key"));
    QVERIFY(probe->lastSpec.body.isEmpty());
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));

    probe->success(QByteArray(R"({"data":[{"id":"gpt-4o"},{"id":"gpt-4.1-mini"},{"id":"gpt-4o"}]})"));

    QVERIFY(successCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void AiLayerTests::fetchModelsForGeminiFiltersToGenerateContentModels() {
    RequestGuard guard;
    const auto probe = std::make_shared<TransportProbe>();
    ProviderTestRunner runner(std::make_unique<FakeTransport>(probe), guard);

    const ProviderProfile profile = makeProfile(
        ProviderProtocol::Gemini,
        QStringLiteral("https://generativelanguage.googleapis.com/v1beta"),
        QStringLiteral("gem-key"),
        QStringLiteral("gemini-2.5-flash"));

    bool successCalled = false;
    QVERIFY(runner.fetchModels(
        profile,
        [&](const QStringList& models) {
            successCalled = true;
            QCOMPARE(models, QStringList({QStringLiteral("gemini-2.5-flash")}));
            QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));
        },
        [&](const QString& error) {
            QFAIL(qPrintable(QStringLiteral("unexpected failure: %1").arg(error)));
        }));

    QCOMPARE(probe->lastSpec.url.toString(),
             QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models?key=gem-key"));
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::TestingProvider));

    probe->success(QByteArray(
        R"({"models":[{"name":"models/gemini-2.5-flash","baseModelId":"gemini-2.5-flash","supportedGenerationMethods":["generateContent"]},{"name":"models/text-embedding-004","baseModelId":"text-embedding-004","supportedGenerationMethods":["embedContent"]}]})"));

    QVERIFY(successCalled);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

QTEST_APPLESS_MAIN(AiLayerTests)

#include "test_ai_layer.moc"
