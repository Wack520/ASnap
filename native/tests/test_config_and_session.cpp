#include <array>

#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "ai/sample_image_factory.h"
#include "chat/chat_session.h"
#include "config/app_config.h"
#include "config/config_store.h"
#include "config/provider_preset.h"
#include "ui/panel_placement.h"

using namespace ais::ai;
using namespace ais::chat;
using namespace ais::config;
using namespace ais::ui;

class ConfigAndSessionTests final : public QObject {
    Q_OBJECT

private slots:
    void configRoundTripsActiveProfile();
    void configRoundTripsCustomFirstPrompt();
    void configMigratesLegacyDefaultFirstPromptV1();
    void configMigratesLegacyDefaultFirstPromptV2();
    void presetTableCoversAllSupportedProtocols();
    void beginWithCaptureResetsConversationAndStoresNewImage();
    void failAssistantResponseReusesTrailingAssistantMessage();
    void choosePanelPositionPrefersVisibleRightSide();
    void choosePanelPositionFallsBackToLeftWhenRightDoesNotFit();
    void choosePanelPositionFallsBackToBelowWhenRightAndLeftDoNotFit();
    void choosePanelPositionFallsBackToAboveWhenRightLeftAndBelowDoNotFit();
    void choosePanelPositionClampsInsideScreenWhenNothingFits();
    void sampleImageFactoryReturnsPngBytes();
};

void ConfigAndSessionTests::configRoundTripsActiveProfile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ConfigStore store(tempDir.filePath("app-config.json"));

    AppConfig expected;
    expected.activeProfile.protocol = ProviderProtocol::OpenAiCompatible;
    expected.activeProfile.baseUrl = QStringLiteral("https://api.example.test/v1");
    expected.activeProfile.apiKey = QStringLiteral("top-secret");
    expected.activeProfile.model = QStringLiteral("gpt-test");
    expected.aiShortcut = QStringLiteral("Ctrl+Shift+A");
    expected.screenshotShortcut = QStringLiteral("Ctrl+Shift+S");
    expected.theme = QStringLiteral("night");
    expected.opacity = 0.72;
    expected.panelColor = QStringLiteral("#223344");
    expected.panelTextColor = QStringLiteral("#f8fafc");
    expected.panelBorderColor = QStringLiteral("#6b7280");
    expected.chatPanelSize = QSize(612, 560);
    expected.settingsDialogSize = QSize(688, 604);
    expected.launchAtLogin = true;

    QVERIFY(store.save(expected));

    const AppConfig loaded = store.load();
    QCOMPARE(static_cast<int>(loaded.activeProfile.protocol),
             static_cast<int>(expected.activeProfile.protocol));
    QCOMPARE(loaded.activeProfile.baseUrl, expected.activeProfile.baseUrl);
    QCOMPARE(loaded.activeProfile.apiKey, expected.activeProfile.apiKey);
    QCOMPARE(loaded.activeProfile.model, expected.activeProfile.model);
    QCOMPARE(loaded.aiShortcut, expected.aiShortcut);
    QCOMPARE(loaded.screenshotShortcut, expected.screenshotShortcut);
    QCOMPARE(loaded.theme, expected.theme);
    QCOMPARE(loaded.opacity, expected.opacity);
    QCOMPARE(loaded.panelColor, expected.panelColor);
    QCOMPARE(loaded.panelTextColor, expected.panelTextColor);
    QCOMPARE(loaded.panelBorderColor, expected.panelBorderColor);
    QCOMPARE(loaded.chatPanelSize, expected.chatPanelSize);
    QCOMPARE(loaded.settingsDialogSize, expected.settingsDialogSize);
    QCOMPARE(loaded.launchAtLogin, expected.launchAtLogin);
}

void ConfigAndSessionTests::configRoundTripsCustomFirstPrompt() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ConfigStore store(tempDir.filePath("app-config.json"));

    AppConfig expected;
    expected.firstPrompt = QStringLiteral("请用中文总结截图内容，并先指出风险。");

    QVERIFY(store.save(expected));

    const AppConfig loaded = store.load();
    QCOMPARE(loaded.firstPrompt, expected.firstPrompt);
}

void ConfigAndSessionTests::configMigratesLegacyDefaultFirstPromptV1() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ConfigStore store(tempDir.filePath("app-config.json"));

    AppConfig expected;
    expected.firstPrompt = QStringLiteral(
        "请只分析我框选到的截图内容，忽略截图工具本身的边框、按钮、输入框等界面元素。"
        "如果截图为空白、选错区域、内容不清晰或无法判断，请明确告诉我。"
        "回答尽量简洁，优先给出有用结论。");

    QVERIFY(store.save(expected));

    const AppConfig loaded = store.load();
    QCOMPARE(loaded.firstPrompt, defaultFirstPromptText());
}

void ConfigAndSessionTests::configMigratesLegacyDefaultFirstPromptV2() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ConfigStore store(tempDir.filePath("app-config.json"));

    AppConfig expected;
    expected.firstPrompt = QStringLiteral(
        "请只分析我框选到的截图内容，忽略截图工具本身的边框、按钮、输入框等界面元素。"
        "如果截图为空白、选错区域、内容不清晰或无法判断，请明确告诉我。");

    QVERIFY(store.save(expected));

    const AppConfig loaded = store.load();
    QCOMPARE(loaded.firstPrompt, defaultFirstPromptText());
}

void ConfigAndSessionTests::presetTableCoversAllSupportedProtocols() {
    struct PresetExpectation {
        ProviderProtocol protocol;
        QString baseUrl;
        QString model;
    };

    const std::array cases{
        PresetExpectation{
            .protocol = ProviderProtocol::OpenAiChat,
            .baseUrl = QStringLiteral("https://api.openai.com/v1"),
            .model = QStringLiteral("gpt-4.1-mini"),
        },
        PresetExpectation{
            .protocol = ProviderProtocol::OpenAiResponses,
            .baseUrl = QStringLiteral("https://api.openai.com/v1"),
            .model = QStringLiteral("gpt-4.1-mini"),
        },
        PresetExpectation{
            .protocol = ProviderProtocol::OpenAiCompatible,
            .baseUrl = QStringLiteral("https://api.openai.com/v1"),
            .model = QStringLiteral("gpt-4.1-mini"),
        },
        PresetExpectation{
            .protocol = ProviderProtocol::Gemini,
            .baseUrl = QStringLiteral("https://generativelanguage.googleapis.com/v1beta"),
            .model = QStringLiteral("gemini-2.5-flash"),
        },
        PresetExpectation{
            .protocol = ProviderProtocol::Claude,
            .baseUrl = QStringLiteral("https://api.anthropic.com/v1"),
            .model = QStringLiteral("claude-sonnet-4-0"),
        },
    };

    for (const auto& item : cases) {
        const ProviderPreset preset = presetFor(item.protocol);

        QCOMPARE(static_cast<int>(preset.protocol), static_cast<int>(item.protocol));
        QCOMPARE(preset.defaultBaseUrl, item.baseUrl);
        QCOMPARE(preset.defaultModel, item.model);
        QVERIFY2(!preset.label.isEmpty(), "preset label should not be empty");
        QVERIFY2(!preset.modelHint.isEmpty(), "preset model hint should not be empty");
    }
}

void ConfigAndSessionTests::beginWithCaptureResetsConversationAndStoresNewImage() {
    ChatSession session;
    session.beginWithCapture(QByteArray("first-image"));
    session.addUserText(QStringLiteral("hello"));
    session.addAssistantText(QStringLiteral("hi"));

    session.beginWithCapture(QByteArray("second-image"));

    const auto& messages = session.messages();
    QCOMPARE(messages.size(), 1);
    QCOMPARE(static_cast<int>(messages.constFirst().role), static_cast<int>(ChatRole::User));
    QCOMPARE(messages.constFirst().imageBytes, QByteArray("second-image"));
    QVERIFY(messages.constFirst().text.isEmpty());
}

void ConfigAndSessionTests::failAssistantResponseReusesTrailingAssistantMessage() {
    ChatSession session;
    session.beginWithCapture(QByteArray("image"));
    session.addUserText(QStringLiteral("hello"));
    session.beginAssistantResponse();
    session.finalizeAssistantResponse();

    session.failAssistantResponse(QStringLiteral("(empty response)"));

    const auto& messages = session.messages();
    QCOMPARE(messages.size(), 3);
    QCOMPARE(static_cast<int>(messages.constLast().role), static_cast<int>(ChatRole::Assistant));
    QCOMPARE(messages.constLast().text, QStringLiteral("(empty response)"));
}

void ConfigAndSessionTests::choosePanelPositionPrefersVisibleRightSide() {
    const QRect anchor(100, 120, 48, 36);
    const QSize panelSize(120, 90);
    const QRect screen(0, 0, 500, 400);

    QCOMPARE(choosePanelPosition(anchor, panelSize, screen), QPoint(anchor.right() + 1, anchor.top()));
}

void ConfigAndSessionTests::choosePanelPositionFallsBackToLeftWhenRightDoesNotFit() {
    const QRect anchor(240, 40, 40, 40);
    const QSize panelSize(100, 80);
    const QRect screen(0, 0, 300, 200);

    QCOMPARE(choosePanelPosition(anchor, panelSize, screen), QPoint(anchor.left() - panelSize.width(), anchor.top()));
}

void ConfigAndSessionTests::choosePanelPositionFallsBackToBelowWhenRightAndLeftDoNotFit() {
    const QRect anchor(90, 20, 60, 40);
    const QSize panelSize(120, 70);
    const QRect screen(0, 0, 220, 200);

    QCOMPARE(choosePanelPosition(anchor, panelSize, screen), QPoint(anchor.left(), anchor.bottom() + 1));
}

void ConfigAndSessionTests::choosePanelPositionFallsBackToAboveWhenRightLeftAndBelowDoNotFit() {
    const QRect anchor(90, 150, 60, 40);
    const QSize panelSize(120, 70);
    const QRect screen(0, 0, 220, 220);

    QCOMPARE(choosePanelPosition(anchor, panelSize, screen), QPoint(anchor.left(), anchor.top() - panelSize.height()));
}

void ConfigAndSessionTests::choosePanelPositionClampsInsideScreenWhenNothingFits() {
    const QRect anchor(60, 60, 30, 20);
    const QSize panelSize(200, 140);
    const QRect screen(20, 30, 120, 90);

    const QPoint position = choosePanelPosition(anchor, panelSize, screen);

    QCOMPARE(position, screen.topLeft());
    QVERIFY(screen.contains(position));
}

void ConfigAndSessionTests::sampleImageFactoryReturnsPngBytes() {
    const QByteArray png = SampleImageFactory::buildPng();
    QImage decoded;

    QVERIFY(!png.isEmpty());
    QCOMPARE(png.left(8), QByteArray("\x89PNG\r\n\x1a\n", 8));
    QVERIFY(decoded.loadFromData(png, "PNG"));
    QCOMPARE(decoded.size(), QSize(96, 72));
}

QTEST_APPLESS_MAIN(ConfigAndSessionTests)

#include "test_config_and_session.moc"
