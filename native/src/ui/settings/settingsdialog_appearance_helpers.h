#pragma once

#include <QColor>
#include <QString>

namespace ais::ui::settings_appearance {

[[nodiscard]] QString effectiveThemeName(const QString& theme);
[[nodiscard]] QColor fallbackSurfaceColor(const QString& theme);
[[nodiscard]] QColor autoTextColor(const QColor& background, const QString& theme);
[[nodiscard]] QColor mutedTextColorForTheme(const QString& theme);
[[nodiscard]] QColor autoBorderColor(const QColor& surfaceColor, const QString& theme);
[[nodiscard]] QString serializeColor(const QColor& color);
[[nodiscard]] QString colorButtonStyle(const QColor& background,
                                       const QColor& foreground,
                                       const QString& theme);
[[nodiscard]] QString dialogStyleSheetForTheme(const QString& theme);

}  // namespace ais::ui::settings_appearance
