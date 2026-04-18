#include "ui/chat/floating_chat_panel_helpers.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QScreen>

#include "ui/chat/chat_markdown_renderer.h"

namespace ais::ui::floating_chat_panel_helpers {

namespace {

using ais::chat::ChatRole;

[[nodiscard]] QColor fallbackSurfaceColor(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#f6f8fa"))
        : QColor(QStringLiteral("#101214"));
}

[[nodiscard]] QColor autoTextColor(const QColor& surfaceColor, const QString& theme) {
    if (surfaceColor.isValid()) {
        return surfaceColor.lightnessF() < 0.52
            ? QColor(QStringLiteral("#f7f8fa"))
            : QColor(QStringLiteral("#15181d"));
    }

    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#15181d"))
        : QColor(QStringLiteral("#f7f8fa"));
}

[[nodiscard]] QColor defaultBorderColorForTheme(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#d0d7de"))
        : QColor(QStringLiteral("#22262b"));
}

[[nodiscard]] QString cssColor(const QColor& color, int alphaOverride = -1) {
    QColor actual = color;
    if (alphaOverride >= 0) {
        actual.setAlpha(qBound(0, alphaOverride, 255));
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(actual.red())
        .arg(actual.green())
        .arg(actual.blue())
        .arg(actual.alpha());
}

}  // namespace

QString effectiveThemeName(const QString& theme) {
    if (theme == QStringLiteral("light") || theme == QStringLiteral("dark")) {
        return theme;
    }

    const QColor windowColor = qApp != nullptr
        ? qApp->palette().color(QPalette::Window)
        : QColor(Qt::white);
    return windowColor.lightness() < 128 ? QStringLiteral("dark") : QStringLiteral("light");
}

QString statusText(bool busy, const QString& status) {
    if (!status.isEmpty()) {
        return status;
    }
    return busy ? QStringLiteral("处理中…") : QStringLiteral("就绪");
}

QString htmlForMessage(const ais::chat::ChatMessage& message,
                       const QString& theme,
                       int* copyCounter,
                       QHash<QString, QString>* copyPayloads) {
    const QString role = message.role == ChatRole::Assistant ? QStringLiteral("AI") : QStringLiteral("你");

    RenderedMarkdown rendered = renderMarkdownWithCodeTools(message.text, theme, copyCounter);
    QString body = rendered.html;
    if (copyPayloads != nullptr) {
        for (auto it = rendered.copyPayloads.cbegin(); it != rendered.copyPayloads.cend(); ++it) {
            copyPayloads->insert(it.key(), it.value());
        }
    }
    if (message.hasImage()) {
        body.append(QStringLiteral("<div class=\"attachment-note\">已附带截图</div>"));
    }
    if (body.isEmpty()) {
        body = QStringLiteral("&nbsp;");
    }

    const QString bubbleClass =
        message.role == ChatRole::Assistant ? QStringLiteral("assistant") : QStringLiteral("user");
    const bool hasVisibleAssistantText =
        message.role == ChatRole::Assistant && !message.text.trimmed().isEmpty();
    const QString streamingBadge = message.streaming && hasVisibleAssistantText
        ? QStringLiteral("<span class=\"streaming-badge\">流式输出中…</span>")
        : QString();

    return QStringLiteral("<div class=\"bubble %1\"><div class=\"role\">%2%3</div><div class=\"body\">%4</div></div>")
        .arg(bubbleClass, role, streamingBadge, body);
}

QColor resolveSurfaceColor(const QString& theme, const QString& requestedColor) {
    const QColor parsed(requestedColor.trimmed());
    if (parsed.isValid()) {
        return parsed;
    }
    return fallbackSurfaceColor(theme);
}

QColor resolveTextColor(const QString& theme,
                        const QColor& surfaceColor,
                        const QString& requestedColor) {
    const QColor parsed(requestedColor.trimmed());
    if (parsed.isValid()) {
        return parsed;
    }
    return autoTextColor(surfaceColor, theme);
}

QColor mutedTextColorForTheme(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#5f6b7a"))
        : QColor(QStringLiteral("#9ea7b3"));
}

QColor autoBorderColor(const QColor& surfaceColor, const QString& theme) {
    if (!surfaceColor.isValid()) {
        return defaultBorderColorForTheme(theme);
    }

    QColor border = effectiveThemeName(theme) == QStringLiteral("light")
        ? surfaceColor.darker(114)
        : surfaceColor.lighter(126);
    if (qAbs(border.lightness() - surfaceColor.lightness()) < 14) {
        border = defaultBorderColorForTheme(theme);
    }
    border.setAlpha(255);
    return border;
}

QColor resolveBorderColor(const QString& theme,
                          const QColor& surfaceColor,
                          const QString& requestedColor) {
    const QColor parsed(requestedColor.trimmed());
    if (parsed.isValid()) {
        return parsed;
    }
    return autoBorderColor(surfaceColor, theme);
}

QString serializeColor(const QColor& color) {
    if (!color.isValid()) {
        return {};
    }
    return color.alpha() < 255
        ? color.name(QColor::HexArgb)
        : color.name(QColor::HexRgb);
}

QString styleSheetForTheme(const QString& theme,
                           const QColor& surfaceColor,
                           const QColor& textColor,
                           const QColor& mutedTextColor,
                           const QColor& lineColor,
                           const int surfaceAlpha) {
    const QString subtleSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.74), 255));
    const QString strongerSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.82) + 2, 255));
    const QString borderColor = cssColor(lineColor, effectiveThemeName(theme) == QStringLiteral("light") ? 230 : 132);
    const QString selectionColor = effectiveThemeName(theme) == QStringLiteral("light")
        ? QStringLiteral("#0969da")
        : QStringLiteral("#34404d");

    return QStringLiteral(
        "FloatingChatPanel { background: transparent; color: %1; border: none; font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; }"
        "QTextBrowser { background: transparent; color: %1; border: none; border-radius: 14px; selection-background-color: %6; selection-color: #ffffff; }"
        "QLineEdit { color: %1; selection-background-color: %6; selection-color: #ffffff; }"
        "QFrame#chatInputShell { background: %4; border: 1px solid %5; border-radius: 18px; }"
        "QFrame#chatInputShell:disabled { background: %3; }"
        "QToolButton { color: %2; border: none; padding: 0; text-align: left; background: transparent; }"
        "QLabel { color: %1; background: transparent; }"
        "QLabel#statusBarLabel { color: %2; font-size: 11px; font-weight: 500; min-height: 16px; max-height: 16px; }"
        "QLineEdit#chatInput { background: transparent; border: none; padding: 0 2px 0 0; min-height: 18px; }"
        "QLineEdit#chatInput:focus { border: none; }"
        "QPushButton#sendButton { background: transparent; color: %1; border: none; border-radius: 13px; padding: 0; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; font-size: 15px; font-weight: 700; }"
        "QPushButton#sendButton:hover { background: %3; }"
        "QPushButton#sendButton:disabled { color: %2; background: transparent; }"
        "QToolButton#dismissButton { background: transparent; color: %2; border: 1px solid transparent; border-radius: 9px; padding: 0; min-width: 18px; min-height: 18px; }"
        "QToolButton#dismissButton:hover { color: %1; border-color: %5; background: %4; }"
        "QToolButton#dismissButton:pressed { background: %3; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 0 4px 0; }"
        "QScrollBar::handle:vertical { background: %5; min-height: 24px; border-radius: 5px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; border: none; }")
        .arg(textColor.name(),
             mutedTextColor.name(),
             strongerSurface,
             subtleSurface,
             borderColor,
             selectionColor);
}

QString historyDocumentCss(const QString& theme,
                           const QColor& surfaceColor,
                           const QColor& textColor,
                           const QColor& mutedTextColor,
                           const QColor& lineColor,
                           const int surfaceAlpha) {
    const QString codeSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.78) + 4, 255));
    const QString toolbarSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.82) + 6, 255));
    const QString subtleBorder = cssColor(lineColor, effectiveThemeName(theme) == QStringLiteral("light") ? 120 : 64);

    return QStringLiteral(
        "body { font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI', sans-serif; color: %1; background: transparent; }"
        ".bubble { margin: 12px 0; padding: 0; border: none; background: transparent; }"
        ".role { font-weight: 700; margin-bottom: 8px; }"
        ".body p { margin: 0 0 8px 0; line-height: 1.65; }"
        ".body h1, .body h2, .body h3 { margin: 12px 0 8px 0; }"
        ".body ul, .body ol { margin: 0 0 8px 18px; }"
        ".body blockquote { margin: 8px 0; padding: 2px 0 2px 12px; border-left: 3px solid %3; color: %2; }"
        ".body pre { margin: 0; padding: 10px 12px; border-radius: 0 0 8px 8px; border: 0; background: %4; color: %1; font-family: 'Cascadia Code', 'Consolas', monospace; white-space: pre-wrap; }"
        ".body code { padding: 1px 4px; border-radius: 4px; background: %4; color: %1; font-family: 'Cascadia Code', 'Consolas', monospace; }"
        ".body pre code { padding: 0; background: transparent; color: inherit; }"
        ".body table { margin: 8px 0; border-collapse: collapse; }"
        ".body th, .body td { border: 1px solid %3; padding: 6px 8px; }"
        ".body a { color: %1; text-decoration: underline; }"
        ".code-card { margin: 8px 0; border: 1px solid %6; border-radius: 10px; overflow: hidden; background: %4; }"
        ".code-toolbar { display: flex; justify-content: space-between; align-items: center; padding: 6px 10px; background: %5; border-bottom: 1px solid %6; }"
        ".code-language { font-family: 'Segoe UI'; font-size: 11px; text-transform: uppercase; color: %2; }"
        ".code-copy { font-family: 'Segoe UI'; font-size: 12px; color: %1; text-decoration: none; }"
        ".streaming-badge { margin-left: 8px; font-size: 11px; color: %2; font-weight: 400; }"
        ".attachment-note { margin-top: 8px; color: %2; font-style: italic; }")
        .arg(textColor.name(),
             mutedTextColor.name(),
             lineColor.name(),
             codeSurface,
             toolbarSurface,
             subtleBorder);
}

QRect availableScreenGeometryForRect(const QRect& rect) {
    if (QScreen* screen = QGuiApplication::screenAt(rect.center()); screen != nullptr) {
        return screen->availableGeometry();
    }

    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        return screen->availableGeometry();
    }

    return rect;
}

QRect clampGeometryToScreen(const QRect& geometry) {
    const QRect screen = availableScreenGeometryForRect(geometry);
    const int maxAllowedWidth = qMax(280, screen.width() - 12);
    const int width = qMin(geometry.width(), maxAllowedWidth);
    const int clampedLeft = screen.left() + qMax(0, screen.width() - width);
    const int clampedTop = screen.top() + qMax(0, screen.height() - geometry.height());
    return QRect(QPoint(qBound(screen.left(), geometry.left(), clampedLeft),
                        qBound(screen.top(), geometry.top(), clampedTop)),
                 QSize(width, geometry.height()));
}

int maximumPanelWidthForRect(const QRect& geometry,
                             const int minimumPanelWidth,
                             const int maximumPanelWidth) {
    const QRect screen = availableScreenGeometryForRect(geometry);
    return qMax(minimumPanelWidth, qMin(maximumPanelWidth, screen.width() - 12));
}

}  // namespace ais::ui::floating_chat_panel_helpers
