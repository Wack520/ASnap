#pragma once

#include <QColor>
#include <QHash>
#include <QRect>
#include <QString>

#include "chat/chat_message.h"

namespace ais::ui::floating_chat_panel_helpers {

[[nodiscard]] QString effectiveThemeName(const QString& theme);
[[nodiscard]] QString statusText(bool busy, const QString& status);
[[nodiscard]] QString htmlForMessage(const ais::chat::ChatMessage& message,
                                     const QString& theme,
                                     int* copyCounter,
                                     QHash<QString, QString>* copyPayloads);

[[nodiscard]] QColor resolveSurfaceColor(const QString& theme, const QString& requestedColor);
[[nodiscard]] QColor resolveTextColor(const QString& theme,
                                      const QColor& surfaceColor,
                                      const QString& requestedColor);
[[nodiscard]] QColor mutedTextColorForTheme(const QString& theme);
[[nodiscard]] QColor mutedTextColorForSurface(const QColor& surfaceColor, const QString& theme);
[[nodiscard]] QColor autoBorderColor(const QColor& surfaceColor, const QString& theme);
[[nodiscard]] QColor resolveBorderColor(const QString& theme,
                                        const QColor& surfaceColor,
                                        const QString& requestedColor);

[[nodiscard]] QString serializeColor(const QColor& color);
[[nodiscard]] QString styleSheetForTheme(const QString& theme,
                                         const QColor& surfaceColor,
                                         const QColor& textColor,
                                         const QColor& mutedTextColor,
                                         const QColor& lineColor,
                                         int surfaceAlpha);
[[nodiscard]] QString historyDocumentCss(const QString& theme,
                                         const QColor& surfaceColor,
                                         const QColor& textColor,
                                         const QColor& mutedTextColor,
                                         const QColor& lineColor,
                                         int surfaceAlpha);

[[nodiscard]] QRect availableScreenGeometryForRect(const QRect& rect);
[[nodiscard]] QRect clampGeometryToScreen(const QRect& geometry);
[[nodiscard]] int maximumPanelWidthForRect(const QRect& geometry,
                                           int minimumPanelWidth,
                                           int maximumPanelWidth);

}  // namespace ais::ui::floating_chat_panel_helpers
