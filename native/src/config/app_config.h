#pragma once

#include <QString>
#include <QSize>

#include "capture/capture_mode.h"
#include "config/provider_profile.h"

namespace ais::config {

[[nodiscard]] inline QString defaultFirstPromptText() {
    return QStringLiteral("请只分析我框选到的截图内容。");
}

struct AppConfig {
    ProviderProfile activeProfile;
    QString aiShortcut = QStringLiteral("Ctrl+Shift+A");
    QString screenshotShortcut = QStringLiteral("Ctrl+Shift+S");
    QString theme = QStringLiteral("system");
    double opacity = 0.92;
    QString panelColor = QStringLiteral("#101214");
    QString panelTextColor;
    QString panelBorderColor;
    QSize chatPanelSize;
    QSize settingsDialogSize;
    capture::CaptureMode captureMode = capture::CaptureMode::Standard;
    bool launchAtLogin = false;
    QString firstPrompt = defaultFirstPromptText();

    bool operator==(const AppConfig&) const = default;
};

}  // namespace ais::config
