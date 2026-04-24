#pragma once

#include <optional>

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QSize>

#include "capture/capture_mode.h"
#include "config/provider_profile.h"

namespace ais::config {

[[nodiscard]] inline QString defaultFirstPromptText() {
    return QStringLiteral("请只分析我框选到的截图内容。");
}

[[nodiscard]] inline QString defaultTextQueryPromptText() {
    return QStringLiteral("请简洁的解释这段文本");
}

[[nodiscard]] inline QSize defaultChatPanelSize() {
    return QSize(360, 560);
}

[[nodiscard]] inline QSize defaultSettingsDialogSize() {
    return QSize(520, 560);
}

[[nodiscard]] inline QString defaultTextQueryShortcut() {
    return QStringLiteral("Ctrl+Shift+A");
}

[[nodiscard]] inline QString defaultAiCaptureShortcut() {
    return QStringLiteral("Ctrl+Shift+Q");
}

[[nodiscard]] inline QString defaultScreenshotShortcut() {
    return QStringLiteral("Ctrl+Shift+S");
}

struct AppConfig {
    ProviderProfile activeProfile;
    QString textQueryShortcut = defaultTextQueryShortcut();
    QString aiShortcut = defaultAiCaptureShortcut();
    QString screenshotShortcut = defaultScreenshotShortcut();
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
    QString textQueryPrompt = defaultTextQueryPromptText();

    bool operator==(const AppConfig&) const = default;
};

[[nodiscard]] inline QString normalizedShortcutValue(const QString& value,
                                                     const QString& fallback) {
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

inline void normalizeShortcutAssignments(AppConfig& config) {
    config.textQueryShortcut =
        normalizedShortcutValue(config.textQueryShortcut, defaultTextQueryShortcut());
    config.aiShortcut =
        normalizedShortcutValue(config.aiShortcut, defaultAiCaptureShortcut());
    config.screenshotShortcut =
        normalizedShortcutValue(config.screenshotShortcut, defaultScreenshotShortcut());

    const auto conflictsWith = [](const QString& candidate, const QStringList& used) {
        return used.contains(candidate, Qt::CaseInsensitive);
    };
    const auto assignDistinct = [&conflictsWith](QString* value,
                                                 const QStringList& candidates,
                                                 const QStringList& used) {
        if (value == nullptr) {
            return;
        }
        for (const QString& candidate : candidates) {
            if (!conflictsWith(candidate, used)) {
                *value = candidate;
                return;
            }
        }
    };

    if (config.aiShortcut.compare(config.textQueryShortcut, Qt::CaseInsensitive) == 0) {
        assignDistinct(&config.aiShortcut,
                       {
                           defaultAiCaptureShortcut(),
                           QStringLiteral("Ctrl+Shift+W"),
                           QStringLiteral("Ctrl+Shift+E"),
                       },
                       {config.textQueryShortcut, config.screenshotShortcut});
    }

    if (config.screenshotShortcut.compare(config.textQueryShortcut, Qt::CaseInsensitive) == 0 ||
        config.screenshotShortcut.compare(config.aiShortcut, Qt::CaseInsensitive) == 0) {
        assignDistinct(&config.screenshotShortcut,
                       {
                           defaultScreenshotShortcut(),
                           QStringLiteral("Ctrl+Shift+D"),
                           QStringLiteral("Ctrl+Shift+F"),
                       },
                       {config.textQueryShortcut, config.aiShortcut});
    }

    if (config.aiShortcut.compare(config.textQueryShortcut, Qt::CaseInsensitive) == 0 ||
        config.aiShortcut.compare(config.screenshotShortcut, Qt::CaseInsensitive) == 0) {
        assignDistinct(&config.aiShortcut,
                       {
                           defaultAiCaptureShortcut(),
                           QStringLiteral("Ctrl+Shift+W"),
                           QStringLiteral("Ctrl+Shift+E"),
                       },
                       {config.textQueryShortcut, config.screenshotShortcut});
    }
}

}  // namespace ais::config
