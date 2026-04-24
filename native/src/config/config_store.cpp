#include "config/config_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "config/provider_protocol.h"

namespace ais::config {
namespace {

constexpr auto kActiveProfileKey = "activeProfile";
constexpr auto kProtocolKey = "protocol";
constexpr auto kBaseUrlKey = "baseUrl";
constexpr auto kApiKeyKey = "apiKey";
constexpr auto kModelKey = "model";
constexpr auto kShortcutKey = "shortcut";
constexpr auto kTextQueryShortcutKey = "textQueryShortcut";
constexpr auto kAiShortcutKey = "aiShortcut";
constexpr auto kScreenshotShortcutKey = "screenshotShortcut";
constexpr auto kThemeKey = "theme";
constexpr auto kOpacityKey = "opacity";
constexpr auto kPanelColorKey = "panelColor";
constexpr auto kPanelTextColorKey = "panelTextColor";
constexpr auto kPanelBorderColorKey = "panelBorderColor";
constexpr auto kChatPanelSizeKey = "chatPanelSize";
constexpr auto kSettingsDialogSizeKey = "settingsDialogSize";
constexpr auto kSettingsDialogPositionKey = "settingsDialogPosition";
constexpr auto kXKey = "x";
constexpr auto kYKey = "y";
constexpr auto kWidthKey = "width";
constexpr auto kHeightKey = "height";
constexpr auto kCaptureModeKey = "captureMode";
constexpr auto kLaunchAtLoginKey = "launchAtLogin";
constexpr auto kFirstPromptKey = "firstPrompt";
constexpr auto kTextQueryPromptKey = "textQueryPrompt";
constexpr auto kLegacyFirstPromptV1 =
    "请只分析我框选到的截图内容，忽略截图工具本身的边框、按钮、输入框等界面元素。"
    "如果截图为空白、选错区域、内容不清晰或无法判断，请明确告诉我。"
    "回答尽量简洁，优先给出有用结论。";
constexpr auto kLegacyFirstPromptV2 =
    "请只分析我框选到的截图内容，忽略截图工具本身的边框、按钮、输入框等界面元素。"
    "如果截图为空白、选错区域、内容不清晰或无法判断，请明确告诉我。";

[[nodiscard]] bool isLegacyDefaultFirstPrompt(const QString& value) {
    const QString normalized = value.trimmed();
    return normalized == QString::fromUtf8(kLegacyFirstPromptV1) ||
           normalized == QString::fromUtf8(kLegacyFirstPromptV2);
}

[[nodiscard]] QJsonObject toJson(const ProviderProfile& profile) {
    return {
        {kProtocolKey, toString(profile.protocol)},
        {kBaseUrlKey, profile.baseUrl},
        {kApiKeyKey, profile.apiKey},
        {kModelKey, profile.model},
    };
}

[[nodiscard]] ProviderProfile profileFromJson(const QJsonObject& json, ProviderProfile defaults = {}) {
    if (const auto protocol = providerProtocolFromString(json.value(kProtocolKey).toString()); protocol.has_value()) {
        defaults.protocol = *protocol;
    }

    defaults.baseUrl = json.value(kBaseUrlKey).toString(defaults.baseUrl);
    defaults.apiKey = json.value(kApiKeyKey).toString(defaults.apiKey);
    defaults.model = json.value(kModelKey).toString(defaults.model);
    return defaults;
}

[[nodiscard]] QJsonObject toJson(const AppConfig& config) {
    auto sizeToJson = [](const QSize& size) -> QJsonValue {
        if (!size.isValid()) {
            return QJsonValue();
        }

        return QJsonObject{
            {kWidthKey, size.width()},
            {kHeightKey, size.height()},
        };
    };
    auto pointToJson = [](const std::optional<QPoint>& point) -> QJsonValue {
        if (!point.has_value()) {
            return QJsonValue();
        }

        return QJsonObject{
            {kXKey, point->x()},
            {kYKey, point->y()},
        };
    };

    QJsonObject object{
        {kActiveProfileKey, toJson(config.activeProfile)},
        {kTextQueryShortcutKey, config.textQueryShortcut},
        {kAiShortcutKey, config.aiShortcut},
        {kScreenshotShortcutKey, config.screenshotShortcut},
        {kThemeKey, config.theme},
        {kOpacityKey, config.opacity},
        {kPanelColorKey, config.panelColor},
        {kPanelTextColorKey, config.panelTextColor},
        {kPanelBorderColorKey, config.panelBorderColor},
        {kCaptureModeKey, capture::toString(config.captureMode)},
        {kLaunchAtLoginKey, config.launchAtLogin},
        {kFirstPromptKey, config.firstPrompt},
        {kTextQueryPromptKey, config.textQueryPrompt},
    };

    if (const QJsonValue chatPanelSize = sizeToJson(config.chatPanelSize); chatPanelSize.isObject()) {
        object.insert(kChatPanelSizeKey, chatPanelSize);
    }
    if (const QJsonValue settingsDialogSize = sizeToJson(config.settingsDialogSize);
        settingsDialogSize.isObject()) {
        object.insert(kSettingsDialogSizeKey, settingsDialogSize);
    }
    if (const QJsonValue settingsDialogPosition = pointToJson(config.settingsDialogPosition);
        settingsDialogPosition.isObject()) {
        object.insert(kSettingsDialogPositionKey, settingsDialogPosition);
    }

    return object;
}

[[nodiscard]] AppConfig appConfigFromJson(const QJsonObject& json) {
    auto sizeFromJson = [](const QJsonValue& value) -> QSize {
        if (!value.isObject()) {
            return {};
        }

        const QJsonObject object = value.toObject();
        const int width = object.value(kWidthKey).toInt();
        const int height = object.value(kHeightKey).toInt();
        if (width <= 0 || height <= 0) {
            return {};
        }
        return QSize(width, height);
    };
    auto pointFromJson = [](const QJsonValue& value) -> std::optional<QPoint> {
        if (!value.isObject()) {
            return std::nullopt;
        }

        const QJsonObject object = value.toObject();
        if (!object.contains(kXKey) || !object.contains(kYKey)) {
            return std::nullopt;
        }

        return QPoint(object.value(kXKey).toInt(), object.value(kYKey).toInt());
    };

    AppConfig config;
    config.activeProfile = profileFromJson(json.value(kActiveProfileKey).toObject(), config.activeProfile);
    config.textQueryShortcut = json.value(kTextQueryShortcutKey).toString(config.textQueryShortcut);
    config.aiShortcut = json.value(kAiShortcutKey).toString(
        json.value(kShortcutKey).toString(config.aiShortcut));
    config.screenshotShortcut =
        json.value(kScreenshotShortcutKey).toString(config.screenshotShortcut);
    config.theme = json.value(kThemeKey).toString(config.theme);
    config.opacity = json.value(kOpacityKey).toDouble(config.opacity);
    config.panelColor = json.value(kPanelColorKey).toString(config.panelColor);
    config.panelTextColor = json.value(kPanelTextColorKey).toString(config.panelTextColor);
    config.panelBorderColor = json.value(kPanelBorderColorKey).toString(config.panelBorderColor);
    config.chatPanelSize = sizeFromJson(json.value(kChatPanelSizeKey));
    config.settingsDialogSize = sizeFromJson(json.value(kSettingsDialogSizeKey));
    config.settingsDialogPosition = pointFromJson(json.value(kSettingsDialogPositionKey));
    if (const auto captureMode =
            capture::captureModeFromString(json.value(kCaptureModeKey).toString());
        captureMode.has_value()) {
        config.captureMode = *captureMode;
    }
    config.launchAtLogin = json.value(kLaunchAtLoginKey).toBool(config.launchAtLogin);
    config.firstPrompt = json.value(kFirstPromptKey).toString(config.firstPrompt);
    if (isLegacyDefaultFirstPrompt(config.firstPrompt)) {
        config.firstPrompt = defaultFirstPromptText();
    }
    config.textQueryPrompt = json.value(kTextQueryPromptKey).toString(config.textQueryPrompt);
    normalizeShortcutAssignments(config);
    return config;
}

}  // namespace

ConfigStore::ConfigStore(QString filePath) : filePath_(std::move(filePath)) {}

AppConfig ConfigStore::load() const {
    QFile file(filePath_);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return {};
    }

    return appConfigFromJson(document.object());
}

bool ConfigStore::save(const AppConfig& config) const {
    if (filePath_.isEmpty()) {
        return false;
    }

    const QFileInfo info(filePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    const QByteArray payload = QJsonDocument(toJson(config)).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

}  // namespace ais::config
