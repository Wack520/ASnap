#pragma once

#include <QString>
#include <QSize>

#include "config/provider_profile.h"

namespace ais::config {

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
    bool launchAtLogin = false;
    QString firstPrompt = QStringLiteral(
        "请只分析我框选到的截图内容，忽略截图工具本身的边框、按钮、输入框等界面元素。"
        "如果截图为空白、选错区域、内容不清晰或无法判断，请明确告诉我。"
        "回答尽量简洁，优先给出有用结论。");

    bool operator==(const AppConfig&) const = default;
};

}  // namespace ais::config
