#include "ui/settings/settingsdialog_appearance_section.h"

#include <QAbstractSpinBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/settings/settingsdialog_appearance_helpers.h"

namespace ais::ui {

namespace {

using ais::ui::settings_appearance::autoBorderColor;
using ais::ui::settings_appearance::autoTextColor;
using ais::ui::settings_appearance::colorButtonStyle;
using ais::ui::settings_appearance::cssColor;
using ais::ui::settings_appearance::effectiveThemeName;
using ais::ui::settings_appearance::fallbackSurfaceColor;
using ais::ui::settings_appearance::mutedTextColorForTheme;
using ais::ui::settings_appearance::previewDocumentCss;
using ais::ui::settings_appearance::previewDocumentHtml;
using ais::ui::settings_appearance::serializeColor;

[[nodiscard]] QColor elevatedPreviewSurface(const QColor& baseColor,
                                            const QString& theme,
                                            const int lightFactor,
                                            const int darkFactor) {
    const QColor reference = baseColor.isValid() ? baseColor : fallbackSurfaceColor(theme);
    QColor elevated = reference;
    elevated = effectiveThemeName(theme) == QStringLiteral("light")
        ? elevated.darker(lightFactor)
        : elevated.lighter(darkFactor);
    if (qAbs(elevated.lightness() - reference.lightness()) < 6) {
        elevated = effectiveThemeName(theme) == QStringLiteral("light")
            ? QColor(QStringLiteral("#eef2f7"))
            : QColor(QStringLiteral("#181c20"));
    }
    elevated.setAlpha(reference.alpha());
    return elevated;
}

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

    previewSurface_ = new QFrame(this);
    previewSurface_->setObjectName(QStringLiteral("previewSurface"));
    previewSurface_->setFrameShape(QFrame::NoFrame);

    previewTitleLabel_ = new QLabel(QStringLiteral("真实聊天框预览"), previewSurface_);
    previewTitleLabel_->setStyleSheet(QStringLiteral("font-weight: 700;"));

    previewStatusLabel_ = new QLabel(QStringLiteral("当前配色、边框和透明度会实时同步到这个示例悬浮窗。"),
                                     previewSurface_);
    previewStatusLabel_->setWordWrap(true);

    previewReasoningToggle_ = new QToolButton(previewSurface_);
    previewReasoningToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    previewReasoningToggle_->setArrowType(Qt::RightArrow);
    previewReasoningToggle_->setText(QStringLiteral("展开思考"));
    previewReasoningToggle_->setEnabled(false);
    previewReasoningToggle_->setFocusPolicy(Qt::NoFocus);

    previewHistoryView_ = new QTextBrowser(previewSurface_);
    previewHistoryView_->setFrameShape(QFrame::NoFrame);
    previewHistoryView_->setOpenLinks(false);
    previewHistoryView_->setOpenExternalLinks(false);
    previewHistoryView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewHistoryView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewHistoryView_->setMinimumHeight(220);
    previewHistoryView_->setMaximumHeight(220);
    previewHistoryView_->setFocusPolicy(Qt::NoFocus);
    previewHistoryView_->viewport()->setFocusPolicy(Qt::NoFocus);

    previewComposerShell_ = new QFrame(previewSurface_);
    previewComposerShell_->setObjectName(QStringLiteral("previewComposerShell"));
    previewComposerShell_->setMinimumHeight(38);

    previewInputField_ = new QLineEdit(previewComposerShell_);
    previewInputField_->setPlaceholderText(QStringLiteral("继续追问，按 Enter 发送…"));
    previewInputField_->setEnabled(false);
    previewInputField_->setFocusPolicy(Qt::NoFocus);
    previewInputField_->setMinimumHeight(30);
    previewInputField_->setFrame(false);

    previewSendButton_ = new QPushButton(QStringLiteral("↑"), previewComposerShell_);
    previewSendButton_->setEnabled(false);
    previewSendButton_->setFocusPolicy(Qt::NoFocus);
    previewSendButton_->setFixedSize(26, 26);

    auto* previewLayout = new QVBoxLayout(previewSurface_);
    previewLayout->setContentsMargins(14, 12, 14, 12);
    previewLayout->setSpacing(8);
    previewLayout->addWidget(previewTitleLabel_);
    previewLayout->addWidget(previewStatusLabel_);
    previewLayout->addWidget(previewReasoningToggle_);
    previewLayout->addWidget(previewHistoryView_);

    auto* previewComposerLayout = new QHBoxLayout(previewComposerShell_);
    previewComposerLayout->setContentsMargins(12, 5, 8, 5);
    previewComposerLayout->setSpacing(8);
    previewComposerLayout->addWidget(previewInputField_, 1);
    previewComposerLayout->addWidget(previewSendButton_, 0, Qt::AlignRight | Qt::AlignVCenter);
    previewLayout->addWidget(previewComposerShell_);

    auto* previewShell = new QFrame(this);
    previewShell->setObjectName(QStringLiteral("previewShell"));
    auto* previewShellLayout = new QVBoxLayout(previewShell);
    previewShellLayout->setContentsMargins(12, 12, 12, 12);
    previewShellLayout->addWidget(previewSurface_);

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
    rootLayout->addWidget(previewShell);
    rootLayout->addLayout(appearanceForm);
    rootLayout->addWidget(transparencyHint);

    connect(opacityField_,
            &QDoubleSpinBox::valueChanged,
            this,
            [this](double) { refreshPreview(); });
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
    refreshPreview();
}

void SettingsDialogAppearanceSection::setPanelTextColor(const QColor& color) {
    panelTextColorCustomized_ = color.isValid();
    panelTextColor_ = panelTextColorCustomized_ ? color : autoTextColor(panelColor_, currentTheme());
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    }
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialogAppearanceSection::setPanelBorderColor(const QColor& color) {
    panelBorderColorCustomized_ = color.isValid();
    panelBorderColor_ = panelBorderColorCustomized_ ? color : autoBorderColor(panelColor_, currentTheme());
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialogAppearanceSection::restoreAutomaticTextColor() {
    panelTextColorCustomized_ = false;
    panelTextColor_ = autoTextColor(panelColor_, currentTheme());
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    }
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialogAppearanceSection::restoreAutomaticBorderColor() {
    panelBorderColorCustomized_ = false;
    panelBorderColor_ = autoBorderColor(panelColor_, currentTheme());
    refreshColorButtons();
    refreshPreview();
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

void SettingsDialogAppearanceSection::refreshPreview() {
    if (previewSurface_ == nullptr || previewTitleLabel_ == nullptr || previewStatusLabel_ == nullptr ||
        previewHistoryView_ == nullptr || previewInputField_ == nullptr || previewSendButton_ == nullptr ||
        previewReasoningToggle_ == nullptr) {
        return;
    }

    const double normalizedOpacity = opacityField_ != nullptr ? opacityField_->value() / 100.0 : 1.0;
    QColor effectiveColor = panelColor_;
    effectiveColor.setAlpha(qBound(0, qRound(panelColor_.alphaF() * normalizedOpacity * 255.0), 255));

    const QString theme = currentTheme();
    const QColor borderColor = panelBorderColorCustomized_
        ? panelBorderColor_
        : autoBorderColor(panelColor_, theme);
    const QColor mutedColor = mutedTextColorForTheme(theme);
    QColor codeColor = elevatedPreviewSurface(panelColor_, theme, 106, 112);
    QColor chromeColor = elevatedPreviewSurface(panelColor_, theme, 104, 108);
    codeColor.setAlpha(qBound(0, effectiveColor.alpha() + 12, 255));
    chromeColor.setAlpha(qBound(0, effectiveColor.alpha() + 20, 255));
    const QString codeSurface = cssColor(codeColor);
    const QString chromeSurface = cssColor(chromeColor);
    const QString previewBorderCss = effectiveThemeName(theme) == QStringLiteral("dark")
        ? cssColor(borderColor, qMin(borderColor.alpha(), 120))
        : cssColor(borderColor);

    previewSurface_->setProperty("previewColor", serializeColor(panelColor_));
    previewSurface_->setProperty("previewBorderColor", serializeColor(borderColor));
    previewSurface_->setProperty("previewOpacity", normalizedOpacity);
    previewSurface_->setStyleSheet(QStringLiteral(
        "QFrame#previewSurface { background-color: %1; border: 1px solid %2; border-radius: 18px; }")
                                       .arg(effectiveColor.name(effectiveColor.alpha() < 255 ? QColor::HexArgb
                                                                                            : QColor::HexRgb),
                                            previewBorderCss));
    previewTitleLabel_->setStyleSheet(QStringLiteral("font-weight: 700; color: %1;")
                                          .arg(panelTextColor_.name(QColor::HexRgb)));
    previewStatusLabel_->setStyleSheet(QStringLiteral("color: %1;")
                                           .arg(mutedColor.name(QColor::HexRgb)));
    previewReasoningToggle_->setStyleSheet(QStringLiteral(
        "QToolButton { color: %1; border: none; padding: 0; background: transparent; }")
                                                .arg(mutedColor.name(QColor::HexRgb)));
    if (previewComposerShell_ != nullptr) {
        previewComposerShell_->setStyleSheet(QStringLiteral(
            "QFrame#previewComposerShell { background: %1; border: 1px solid %2; border-radius: 18px; }")
                                                 .arg(chromeSurface, previewBorderCss));
    }
    previewInputField_->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; color: %1; border: none; padding: 0 2px 0 0; min-height: 18px; }"
        "QLineEdit:focus { border: none; }")
                                          .arg(panelTextColor_.name(QColor::HexRgb)));
    previewSendButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; border-radius: 13px; padding: 0; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; font-size: 15px; font-weight: 700; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:disabled { color: %3; background: transparent; }")
                                           .arg(panelTextColor_.name(QColor::HexRgb),
                                                chromeSurface,
                                                mutedColor.name(QColor::HexRgb)));

    const QString documentCss = previewDocumentCss(panelTextColor_.name(QColor::HexRgb),
                                                   mutedColor.name(QColor::HexRgb),
                                                   codeSurface,
                                                   chromeSurface,
                                                   borderColor.name(QColor::HexRgb));
    previewHistoryView_->document()->setDefaultStyleSheet(documentCss);
    previewHistoryView_->setHtml(previewDocumentHtml(documentCss));
    previewHistoryView_->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: transparent; color: %1; border: none; }")
                                           .arg(panelTextColor_.name(QColor::HexRgb)));
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
        refreshPreview();
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
        refreshPreview();
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
        refreshPreview();
        return;
    }

    setPanelBorderColor(dialog.currentColor());
}

}  // namespace ais::ui
