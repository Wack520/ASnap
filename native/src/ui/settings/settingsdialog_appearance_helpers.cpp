#include "ui/settings/settingsdialog_appearance_helpers.h"

#include <QApplication>
#include <QPalette>

namespace ais::ui::settings_appearance {
namespace {

[[nodiscard]] QColor defaultBorderColorForTheme(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#d0d7de"))
        : QColor(QStringLiteral("#22262b"));
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

QColor fallbackSurfaceColor(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#f6f8fa"))
        : QColor(QStringLiteral("#101214"));
}

QColor autoTextColor(const QColor& background, const QString& theme) {
    if (background.isValid()) {
        return background.lightnessF() < 0.52
            ? QColor(QStringLiteral("#f7f8fa"))
            : QColor(QStringLiteral("#15181d"));
    }

    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#15181d"))
        : QColor(QStringLiteral("#f7f8fa"));
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

QString serializeColor(const QColor& color) {
    if (!color.isValid()) {
        return {};
    }
    return color.alpha() < 255
        ? color.name(QColor::HexArgb)
        : color.name(QColor::HexRgb);
}

QString colorButtonStyle(const QColor& background,
                         const QColor& foreground,
                         const QString& theme) {
    const QColor border = effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#d0d7de"))
        : QColor(QStringLiteral("#2c3138"));
    return QStringLiteral(
               "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
               "border-radius: 10px; padding: 8px 12px; text-align: left; }")
        .arg(background.name(background.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb),
             foreground.name(QColor::HexRgb),
             border.name(QColor::HexRgb));
}

QString dialogStyleSheetForTheme(const QString& theme) {
    if (effectiveThemeName(theme) == QStringLiteral("light")) {
        return QStringLiteral(
            "QDialog { background-color: #eef2f7; color: #15181d; font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; font-size: 13px; }"
            "QScrollArea, QWidget#settingsScrollContent, QWidget#settingsScrollViewport { background-color: #eef2f7; }"
            "QLabel { color: #15181d; background: transparent; }"
            "QLabel#settingsHeader { font-size: 22px; font-weight: 700; }"
            "QLabel#settingsSubtitle { color: #5f6b7a; }"
            "QLabel#sectionTitle { font-size: 13px; font-weight: 700; color: #445162; margin-top: 4px; }"
            "QFrame#settingsCard { background-color: #ffffff; border: 1px solid #d8dee6; border-radius: 14px; }"
            "QLineEdit, QPlainTextEdit, QComboBox, QKeySequenceEdit, QAbstractSpinBox {"
            " background-color: #ffffff; color: #15181d; border: 1px solid #d0d7de; border-radius: 10px; padding: 7px 10px; selection-background-color: #0969da; selection-color: #ffffff; }"
            "QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QKeySequenceEdit:focus, QAbstractSpinBox:focus { border-color: #0969da; }"
            "QPlainTextEdit { padding: 8px; }"
            "QComboBox::drop-down { border: none; width: 24px; }"
            "QComboBox#modelCombo::drop-down { width: 0px; border: none; }"
            "QComboBox#modelCombo::down-arrow { image: none; width: 0px; height: 0px; }"
            "QComboBox QAbstractItemView { background-color: #ffffff; color: #15181d; selection-background-color: #dbeafe; }"
            "QPushButton { background-color: #ffffff; color: #15181d; border: 1px solid #d0d7de; border-radius: 10px; padding: 8px 14px; }"
            "QPushButton:hover { border-color: #8c959f; }"
            "QPushButton:disabled { background-color: #f3f4f6; color: #9aa1a9; border-color: #e5e7eb; }"
            "QPushButton#modelActionButton { border-radius: 18px; padding: 7px 13px; }"
            "QToolButton#modelPopupButton { background-color: #ffffff; color: #15181d; border: 1px solid #d0d7de; border-radius: 10px; padding: 0; min-width: 32px; min-height: 32px; }"
            "QToolButton#modelPopupButton:hover { border-color: #8c959f; }");
    }

    return QStringLiteral(
        "QDialog { background-color: #0a0c0d; color: #eef2f6; font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; font-size: 13px; }"
        "QScrollArea, QWidget#settingsScrollContent, QWidget#settingsScrollViewport { background-color: #0a0c0d; }"
        "QLabel { color: #eef2f6; background: transparent; }"
        "QLabel#settingsHeader { font-size: 22px; font-weight: 700; }"
        "QLabel#settingsSubtitle { color: #98a1ac; }"
        "QLabel#sectionTitle { font-size: 13px; font-weight: 700; color: #d5dbe3; margin-top: 4px; }"
        "QFrame#settingsCard { background-color: rgba(19,21,23,232); border: 1px solid rgba(255,255,255,0.04); border-radius: 14px; }"
        "QLineEdit, QPlainTextEdit, QComboBox, QKeySequenceEdit, QAbstractSpinBox {"
        " background-color: rgba(255,255,255,0.018); color: #eef2f6; border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; padding: 7px 10px; selection-background-color: #2f3740; selection-color: #ffffff; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QKeySequenceEdit:focus, QAbstractSpinBox:focus { border-color: rgba(255,255,255,0.12); }"
        "QPlainTextEdit { padding: 8px; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox#modelCombo::drop-down { width: 0px; border: none; }"
        "QComboBox#modelCombo::down-arrow { image: none; width: 0px; height: 0px; }"
        "QComboBox QAbstractItemView { background-color: #101214; color: #eef2f6; selection-background-color: #252c34; }"
        "QPushButton { background-color: rgba(255,255,255,0.018); color: #eef2f6; border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; padding: 7px 11px; }"
        "QPushButton:hover { border-color: rgba(255,255,255,0.085); background-color: rgba(255,255,255,0.03); }"
        "QPushButton:disabled { background-color: rgba(255,255,255,0.008); color: #727a84; border-color: rgba(255,255,255,0.035); }"
        "QPushButton#modelActionButton { background-color: rgba(255,255,255,0.016); border-radius: 18px; padding: 7px 13px; }"
        "QPushButton#modelActionButton:hover { background-color: rgba(255,255,255,0.028); }"
        "QToolButton#modelPopupButton { background-color: rgba(255,255,255,0.018); color: #eef2f6; border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; padding: 0; min-width: 32px; min-height: 32px; }"
        "QToolButton#modelPopupButton:hover { border-color: rgba(255,255,255,0.085); background-color: rgba(255,255,255,0.03); }");
}

}  // namespace ais::ui::settings_appearance
