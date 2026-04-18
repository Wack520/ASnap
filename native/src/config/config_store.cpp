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
constexpr auto kAiShortcutKey = "aiShortcut";
constexpr auto kScreenshotShortcutKey = "screenshotShortcut";
constexpr auto kThemeKey = "theme";
constexpr auto kOpacityKey = "opacity";
constexpr auto kPanelColorKey = "panelColor";
constexpr auto kPanelTextColorKey = "panelTextColor";
constexpr auto kPanelBorderColorKey = "panelBorderColor";
constexpr auto kChatPanelSizeKey = "chatPanelSize";
constexpr auto kSettingsDialogSizeKey = "settingsDialogSize";
constexpr auto kWidthKey = "width";
constexpr auto kHeightKey = "height";
constexpr auto kLaunchAtLoginKey = "launchAtLogin";
constexpr auto kFirstPromptKey = "firstPrompt";

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

    QJsonObject object{
        {kActiveProfileKey, toJson(config.activeProfile)},
        {kAiShortcutKey, config.aiShortcut},
        {kScreenshotShortcutKey, config.screenshotShortcut},
        {kThemeKey, config.theme},
        {kOpacityKey, config.opacity},
        {kPanelColorKey, config.panelColor},
        {kPanelTextColorKey, config.panelTextColor},
        {kPanelBorderColorKey, config.panelBorderColor},
        {kLaunchAtLoginKey, config.launchAtLogin},
        {kFirstPromptKey, config.firstPrompt},
    };

    if (const QJsonValue chatPanelSize = sizeToJson(config.chatPanelSize); chatPanelSize.isObject()) {
        object.insert(kChatPanelSizeKey, chatPanelSize);
    }
    if (const QJsonValue settingsDialogSize = sizeToJson(config.settingsDialogSize);
        settingsDialogSize.isObject()) {
        object.insert(kSettingsDialogSizeKey, settingsDialogSize);
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

    AppConfig config;
    config.activeProfile = profileFromJson(json.value(kActiveProfileKey).toObject(), config.activeProfile);
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
    config.launchAtLogin = json.value(kLaunchAtLoginKey).toBool(config.launchAtLogin);
    config.firstPrompt = json.value(kFirstPromptKey).toString(config.firstPrompt);
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
