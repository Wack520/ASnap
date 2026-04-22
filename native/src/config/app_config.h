#pragma once

#include <optional>

#include <QPoint>
#include <QString>
#include <QSize>

#include "capture/capture_mode.h"
#include "config/provider_profile.h"

namespace ais::config {

[[nodiscard]] inline QString defaultFirstPromptText() {
    return QStringLiteral("请只分析我框选到的截图内容。");
}

[[nodiscard]] inline QSize defaultChatPanelSize() {
    return QSize(360, 560);
}

[[nodiscard]] inline QSize defaultSettingsDialogSize() {
    return QSize(520, 560);
}

struct AppConfig {
    ProviderProfile activeProfile;
    QString aiShortcut = QStringLiteral("Ctrl+Shift+A");
    QString screenshotShortcut = QStringLiteral("Ctrl+Shift+S");
    QString theme = QStringLiteral("system");
    double opacity = 0.95;
    QString panelColor = QStringLiteral("#ffffff");
    QString panelTextColor;
    QString panelBorderColor = QStringLiteral("#000000");
    QSize chatPanelSize = defaultChatPanelSize();
    QSize settingsDialogSize = defaultSettingsDialogSize();
    std::optional<QPoint> settingsDialogPosition;
    capture::CaptureMode captureMode = capture::CaptureMode::Standard;
    bool launchAtLogin = false;
    QString firstPrompt = defaultFirstPromptText();

    bool operator==(const AppConfig&) const = default;
};

}  // namespace ais::config
