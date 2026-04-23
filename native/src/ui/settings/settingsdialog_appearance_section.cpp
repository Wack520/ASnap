#include "ui/settings/settingsdialog_appearance_section.h"

#include <QAbstractSpinBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/settings/settingsdialog_appearance_helpers.h"

namespace ais::ui {

namespace {

using ais::ui::settings_appearance::autoBorderColor;
using ais::ui::settings_appearance::autoTextColor;
using ais::ui::settings_appearance::colorButtonStyle;
using ais::ui::settings_appearance::fallbackSurfaceColor;
using ais::ui::settings_appearance::serializeColor;

}  // namespace

SettingsDialogAppearanceSection::SettingsDialogAppearanceSection(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("settingsCard"));

    themeField_ = new QComboBox(this);
    themeField_->addItem(QStringLiteral("System"), QStringLiteral("system"));
    themeField_->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    themeField_->addItem(QStringLiteral("Light"), QStringLiteral("light"));

    opacityField_ = new QDoubleSpinBox(this);
    opacityField_->setRange(0.0, 100.0);
    opacityField_->setSingleStep(5.0);
    opacityField_->setDecimals(0);
    opacityField_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    opacityField_->setSuffix(QStringLiteral("%"));
    opacityField_->setToolTip(QStringLiteral("想要透明背景，直接调这里；0% 接近全透明，100% 完全不透明。"));

    panelColorButton_ = new QPushButton(this);
    panelColorButton_->setMinimumHeight(38);
    panelTextColorButton_ = new QPushButton(this);
    panelTextColorButton_->setMinimumHeight(38);
    panelTextAutoButton_ = new QPushButton(QStringLiteral("恢复自动"), this);
    panelBorderColorButton_ = new QPushButton(this);
    panelBorderColorButton_->setMinimumHeight(38);
    panelBorderAutoButton_ = new QPushButton(QStringLiteral("恢复自动"), this);

    auto* appearanceForm = new QFormLayout();
    appearanceForm->setSpacing(8);
    appearanceForm->addRow(QStringLiteral("主题"), themeField_);
    appearanceForm->addRow(QStringLiteral("背景不透明度"), opacityField_);
    appearanceForm->addRow(QStringLiteral("聊天背景色"), panelColorButton_);

    auto* textColorRow = new QHBoxLayout();
    textColorRow->setSpacing(8);
    textColorRow->addWidget(panelTextColorButton_, 1);
    textColorRow->addWidget(panelTextAutoButton_);
    appearanceForm->addRow(QStringLiteral("字体颜色"), textColorRow);

    auto* borderColorRow = new QHBoxLayout();
    borderColorRow->setSpacing(8);
    borderColorRow->addWidget(panelBorderColorButton_, 1);
    borderColorRow->addWidget(panelBorderAutoButton_);
    appearanceForm->addRow(QStringLiteral("边框颜色"), borderColorRow);

    auto* transparencyHint = new QLabel(QStringLiteral("想要透明背景时，直接调“背景不透明度”，不用手动输入透明色。"),
                                        this);
    transparencyHint->setObjectName(QStringLiteral("settingsSubtitle"));
    transparencyHint->setWordWrap(true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->addLayout(appearanceForm);
    rootLayout->addWidget(transparencyHint);

    connect(panelColorButton_, &QPushButton::clicked, this, &SettingsDialogAppearanceSection::choosePanelColor);
    connect(panelTextColorButton_,
            &QPushButton::clicked,
            this,
            &SettingsDialogAppearanceSection::choosePanelTextColor);
    connect(panelTextAutoButton_,
            &QPushButton::clicked,
            this,
            &SettingsDialogAppearanceSection::restoreAutomaticTextColor);
    connect(panelBorderColorButton_,
            &QPushButton::clicked,
            this,
            &SettingsDialogAppearanceSection::choosePanelBorderColor);
    connect(panelBorderAutoButton_,
            &QPushButton::clicked,
            this,
            &SettingsDialogAppearanceSection::restoreAutomaticBorderColor);
}

void SettingsDialogAppearanceSection::setPanelColor(const QColor& color) {
    panelColor_ = color.isValid() ? color : fallbackSurfaceColor(currentTheme());
    if (!panelTextColorCustomized_) {
        panelTextColor_ = autoTextColor(panelColor_, currentTheme());
    }
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    }
    refreshColorButtons();
}

void SettingsDialogAppearanceSection::setPanelTextColor(const QColor& color) {
    panelTextColorCustomized_ = color.isValid();
    panelTextColor_ = panelTextColorCustomized_ ? color : autoTextColor(panelColor_, currentTheme());
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    }
    refreshColorButtons();
}

void SettingsDialogAppearanceSection::setPanelBorderColor(const QColor& color) {
    panelBorderColorCustomized_ = color.isValid();
    panelBorderColor_ = panelBorderColorCustomized_ ? color : autoBorderColor(panelColor_, currentTheme());
    refreshColorButtons();
}

void SettingsDialogAppearanceSection::restoreAutomaticTextColor() {
    panelTextColorCustomized_ = false;
    panelTextColor_ = autoTextColor(panelColor_, currentTheme());
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    }
    refreshColorButtons();
}

void SettingsDialogAppearanceSection::restoreAutomaticBorderColor() {
    panelBorderColorCustomized_ = false;
    panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    refreshColorButtons();
}

void SettingsDialogAppearanceSection::refreshColorButtons() {
    const QString theme = currentTheme();

    if (panelColorButton_ != nullptr) {
        const QColor buttonTextColor = autoTextColor(panelColor_, theme);
        panelColorButton_->setText(QStringLiteral("选择颜色  %1").arg(serializeColor(panelColor_).toUpper()));
        panelColorButton_->setStyleSheet(colorButtonStyle(panelColor_, buttonTextColor, theme));
    }

    if (panelTextColorButton_ != nullptr) {
        const QString text = panelTextColorCustomized_
            ? QStringLiteral("选择颜色  %1").arg(serializeColor(panelTextColor_).toUpper())
            : QStringLiteral("自动（当前 %1）").arg(serializeColor(panelTextColor_).toUpper());
        panelTextColorButton_->setText(text);
        panelTextColorButton_->setStyleSheet(colorButtonStyle(panelColor_, panelTextColor_, theme));
    }

    if (panelTextAutoButton_ != nullptr) {
        if (!panelTextColorCustomized_) {
            panelTextAutoButton_->setToolTip(QStringLiteral("当前已使用自动字体颜色"));
        } else {
            panelTextAutoButton_->setToolTip(QStringLiteral("恢复为自动计算的高对比字体色"));
        }
    }

    if (panelBorderColorButton_ != nullptr) {
        const QColor buttonTextColor = autoTextColor(panelBorderColor_, theme);
        const QString text = panelBorderColorCustomized_
            ? QStringLiteral("选择颜色  %1").arg(serializeColor(panelBorderColor_).toUpper())
            : QStringLiteral("自动（当前 %1）").arg(serializeColor(panelBorderColor_).toUpper());
        panelBorderColorButton_->setText(text);
        panelBorderColorButton_->setStyleSheet(
            colorButtonStyle(panelBorderColor_, buttonTextColor, theme));
    }

    if (panelBorderAutoButton_ != nullptr) {
        if (!panelBorderColorCustomized_) {
            panelBorderAutoButton_->setToolTip(QStringLiteral("当前已使用自动边框颜色"));
        } else {
            panelBorderAutoButton_->setToolTip(QStringLiteral("恢复为自动计算的边框颜色"));
        }
    }
}

void SettingsDialogAppearanceSection::setBusy(const bool busy) {
    for (QWidget* widget : {
             static_cast<QWidget*>(themeField_),
             static_cast<QWidget*>(opacityField_),
             static_cast<QWidget*>(panelColorButton_),
             static_cast<QWidget*>(panelTextColorButton_),
             static_cast<QWidget*>(panelTextAutoButton_),
             static_cast<QWidget*>(panelBorderColorButton_),
             static_cast<QWidget*>(panelBorderAutoButton_),
         }) {
        if (widget != nullptr) {
            widget->setEnabled(!busy);
        }
    }
}

QString SettingsDialogAppearanceSection::currentTheme() const {
    return themeField_ != nullptr ? themeField_->currentData().toString() : QStringLiteral("dark");
}

void SettingsDialogAppearanceSection::choosePanelColor() {
    const QColor originalColor = panelColor_;
    const QColor originalTextColor = panelTextColor_;
    const bool originalTextCustomized = panelTextColorCustomized_;
    const QColor originalBorderColor = panelBorderColor_;
    const bool originalBorderCustomized = panelBorderColorCustomized_;

    QColorDialog dialog(panelColor_, this);
    dialog.setWindowTitle(QStringLiteral("选择聊天背景色"));
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    connect(&dialog,
            &QColorDialog::currentColorChanged,
            this,
            [this](const QColor& current) {
                if (current.isValid()) {
                    setPanelColor(current);
                }
            });

    if (dialog.exec() != QDialog::Accepted) {
        panelColor_ = originalColor;
        panelTextColor_ = originalTextColor;
        panelTextColorCustomized_ = originalTextCustomized;
        panelBorderColor_ = originalBorderColor;
        panelBorderColorCustomized_ = originalBorderCustomized;
        refreshColorButtons();
        return;
    }

    setPanelColor(dialog.currentColor());
}

void SettingsDialogAppearanceSection::choosePanelTextColor() {
    const QColor originalTextColor = panelTextColor_;
    const bool originalTextCustomized = panelTextColorCustomized_;
    const QColor originalBorderColor = panelBorderColor_;
    const bool originalBorderCustomized = panelBorderColorCustomized_;

    QColorDialog dialog(panelTextColor_, this);
    dialog.setWindowTitle(QStringLiteral("选择字体颜色"));
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    connect(&dialog,
            &QColorDialog::currentColorChanged,
            this,
            [this](const QColor& current) {
                if (current.isValid()) {
                    setPanelTextColor(current);
                }
            });

    if (dialog.exec() != QDialog::Accepted) {
        panelTextColor_ = originalTextColor;
        panelTextColorCustomized_ = originalTextCustomized;
        panelBorderColor_ = originalBorderColor;
        panelBorderColorCustomized_ = originalBorderCustomized;
        refreshColorButtons();
        return;
    }

    setPanelTextColor(dialog.currentColor());
}

void SettingsDialogAppearanceSection::choosePanelBorderColor() {
    const QColor originalBorderColor = panelBorderColor_;
    const bool originalBorderCustomized = panelBorderColorCustomized_;

    QColorDialog dialog(panelBorderColor_, this);
    dialog.setWindowTitle(QStringLiteral("选择边框颜色"));
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    connect(&dialog,
            &QColorDialog::currentColorChanged,
            this,
            [this](const QColor& current) {
                if (current.isValid()) {
                    setPanelBorderColor(current);
                }
            });

    if (dialog.exec() != QDialog::Accepted) {
        panelBorderColor_ = originalBorderColor;
        panelBorderColorCustomized_ = originalBorderCustomized;
        refreshColorButtons();
        return;
    }

    setPanelBorderColor(dialog.currentColor());
}

}  // namespace ais::ui
