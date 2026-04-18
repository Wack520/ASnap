#include "ui/settings/settingsdialog.h"

#include <algorithm>

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "config/provider_preset.h"
#include "ui/settings/settingsdialog_appearance_helpers.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace ais::ui {

namespace {

using ais::config::AppConfig;
using ais::config::ProviderPreset;
using ais::config::ProviderProfile;
using ais::config::ProviderProtocol;
using ais::config::presetFor;
using ais::ui::settings_appearance::autoBorderColor;
using ais::ui::settings_appearance::autoTextColor;
using ais::ui::settings_appearance::colorButtonStyle;
using ais::ui::settings_appearance::cssColor;
using ais::ui::settings_appearance::dialogStyleSheetForTheme;
using ais::ui::settings_appearance::effectiveThemeName;
using ais::ui::settings_appearance::fallbackSurfaceColor;
using ais::ui::settings_appearance::mutedTextColorForTheme;
using ais::ui::settings_appearance::previewDocumentCss;
using ais::ui::settings_appearance::previewDocumentHtml;
using ais::ui::settings_appearance::serializeColor;

[[nodiscard]] ProviderProtocol protocolFromCombo(const QComboBox* comboBox) {
    return static_cast<ProviderProtocol>(comboBox->currentData().toInt());
}

[[nodiscard]] QString themedStatusText(bool busy, const QString& status) {
    if (!status.isEmpty()) {
        return status;
    }
    return busy ? QStringLiteral("处理中…") : QStringLiteral("就绪");
}

#ifdef Q_OS_WIN
void applyImmersiveDarkTitleBar(QWidget* widget, const bool dark) {
    if (widget == nullptr) {
        return;
    }

    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd == nullptr) {
        return;
    }

    const BOOL enabled = dark ? TRUE : FALSE;
    constexpr DWORD kUseImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &enabled, sizeof(enabled));
}
#else
void applyImmersiveDarkTitleBar(QWidget*, bool) {}
#endif

}  // namespace

SettingsDialog::SettingsDialog(const AppConfig& config, QWidget* parent)
    : QDialog(parent),
      initialConfig_(config) {
    setWindowTitle(QStringLiteral("ASnap 设置"));
    setMinimumSize(520, 560);
    resize(config.settingsDialogSize.isValid()
               ? config.settingsDialogSize.expandedTo(minimumSize())
               : QSize(540, 560));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    headerLabel_ = new QLabel(QStringLiteral("设置"), this);
    headerLabel_->setObjectName(QStringLiteral("settingsHeader"));
    subtitleLabel_ = new QLabel(QStringLiteral("统一管理服务连接、外观和截图后的首轮提示词。"), this);
    subtitleLabel_->setObjectName(QStringLiteral("settingsSubtitle"));
    subtitleLabel_->setWordWrap(true);

    statusLabel_ = new QLabel(QStringLiteral("就绪"), this);
    statusLabel_->setWordWrap(true);

    protocolSelector_ = new QComboBox(this);
    for (const ProviderProtocol protocol : {
             ProviderProtocol::OpenAiChat,
             ProviderProtocol::OpenAiResponses,
             ProviderProtocol::OpenAiCompatible,
             ProviderProtocol::Gemini,
             ProviderProtocol::Claude,
         }) {
        const ProviderPreset preset = presetFor(protocol);
        protocolSelector_->addItem(preset.label, static_cast<int>(protocol));
    }

    baseUrlField_ = new QLineEdit(this);
    apiKeyField_ = new QLineEdit(this);
    apiKeyField_->setEchoMode(QLineEdit::PasswordEchoOnEdit);

    modelField_ = new QComboBox(this);
    modelField_->setObjectName(QStringLiteral("modelCombo"));
    modelField_->setEditable(true);
    modelField_->setInsertPolicy(QComboBox::NoInsert);
    modelPopupButton_ = new QToolButton(this);
    modelPopupButton_->setObjectName(QStringLiteral("modelPopupButton"));
    modelPopupButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    modelPopupButton_->setArrowType(Qt::DownArrow);
    modelPopupButton_->setToolTip(QStringLiteral("展开模型列表"));
    modelPopupButton_->setCursor(Qt::PointingHandCursor);
    fetchModelsButton_ = new QPushButton(QStringLiteral("获取模型"), this);
    fetchModelsButton_->setObjectName(QStringLiteral("modelActionButton"));
    fetchModelsButton_->setMinimumWidth(72);
    modelActionStatusLabel_ = new QLabel(QStringLiteral("可以先获取模型列表，再做文字 / 图片测试。"), this);
    modelActionStatusLabel_->setObjectName(QStringLiteral("settingsSubtitle"));
    modelActionStatusLabel_->setWordWrap(true);

    aiShortcutField_ = new QKeySequenceEdit(this);
    aiShortcutField_->setMaximumSequenceLength(1);

    screenshotShortcutField_ = new QKeySequenceEdit(this);
    screenshotShortcutField_->setMaximumSequenceLength(1);

    themeField_ = new QComboBox(this);
    themeField_->addItem(QStringLiteral("System"), QStringLiteral("system"));
    themeField_->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    themeField_->addItem(QStringLiteral("Light"), QStringLiteral("light"));

    opacityField_ = new QDoubleSpinBox(this);
    opacityField_->setRange(0.0, 100.0);
    opacityField_->setSingleStep(5.0);
    opacityField_->setDecimals(0);
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
    previewHistoryView_ = new QTextBrowser(previewSurface_);
    previewHistoryView_->setFrameShape(QFrame::NoFrame);
    previewHistoryView_->setOpenLinks(false);
    previewHistoryView_->setOpenExternalLinks(false);
    previewHistoryView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewHistoryView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewHistoryView_->setMinimumHeight(220);
    previewHistoryView_->setMaximumHeight(220);
    previewInputField_ = new QLineEdit(previewSurface_);
    previewInputField_->setPlaceholderText(QStringLiteral("继续追问，按 Enter 发送…"));
    previewInputField_->setEnabled(false);
    previewSendButton_ = new QPushButton(QStringLiteral("↑"), previewSurface_);
    previewSendButton_->setEnabled(false);
    auto* previewLayout = new QVBoxLayout(previewSurface_);
    previewLayout->setContentsMargins(14, 12, 14, 12);
    previewLayout->setSpacing(8);
    previewLayout->addWidget(previewTitleLabel_);
    previewLayout->addWidget(previewStatusLabel_);
    previewLayout->addWidget(previewReasoningToggle_);
    previewLayout->addWidget(previewHistoryView_);
    auto* previewInputRow = new QHBoxLayout();
    previewInputRow->setSpacing(8);
    previewInputRow->addWidget(previewInputField_, 1);
    previewInputRow->addWidget(previewSendButton_);
    previewLayout->addLayout(previewInputRow);

    firstPromptField_ = new QPlainTextEdit(this);
    firstPromptField_->setPlaceholderText(QStringLiteral("请输入截图后自动发送给 AI 的首轮提示词"));
    firstPromptField_->setMinimumHeight(96);
    launchAtLoginCheckBox_ = new QCheckBox(QStringLiteral("Windows 当前用户登录后静默启动"), this);

    testConnectionButton_ = new QPushButton(QStringLiteral("测试文字连接"), this);
    testConnectionButton_->setObjectName(QStringLiteral("modelActionButton"));
    testConnectionButton_->setMinimumWidth(72);
    testImageButton_ = new QPushButton(QStringLiteral("测试图片理解"), this);
    testImageButton_->setObjectName(QStringLiteral("modelActionButton"));
    testImageButton_->setMinimumWidth(72);

    auto* providerCard = new QFrame(this);
    providerCard->setObjectName(QStringLiteral("settingsCard"));
    auto* providerForm = new QFormLayout(providerCard);
    providerForm->setContentsMargins(12, 12, 12, 12);
    providerForm->setSpacing(8);
    providerForm->setHorizontalSpacing(12);
    auto* modelToolsRow = new QWidget(providerCard);
    auto* modelToolsLayout = new QHBoxLayout(modelToolsRow);
    modelToolsLayout->setContentsMargins(0, 0, 0, 0);
    modelToolsLayout->setSpacing(4);
    modelToolsLayout->addWidget(modelField_, 1);
    modelToolsLayout->addWidget(modelPopupButton_);
    modelToolsLayout->addWidget(fetchModelsButton_);
    modelToolsLayout->addWidget(testConnectionButton_);
    modelToolsLayout->addWidget(testImageButton_);

    auto* modelToolsStack = new QWidget(providerCard);
    auto* modelToolsStackLayout = new QVBoxLayout(modelToolsStack);
    modelToolsStackLayout->setContentsMargins(0, 0, 0, 0);
    modelToolsStackLayout->setSpacing(6);
    modelToolsStackLayout->addWidget(modelToolsRow);
    modelToolsStackLayout->addWidget(modelActionStatusLabel_);

    providerForm->addRow(QStringLiteral("状态"), statusLabel_);
    providerForm->addRow(QStringLiteral("协议类型"), protocolSelector_);
    providerForm->addRow(QStringLiteral("Base URL"), baseUrlField_);
    providerForm->addRow(QStringLiteral("API Key"), apiKeyField_);
    providerForm->addRow(QStringLiteral("模型"), modelToolsStack);

    auto* shortcutFieldsRow = new QWidget(providerCard);
    auto* shortcutFieldsLayout = new QHBoxLayout(shortcutFieldsRow);
    shortcutFieldsLayout->setContentsMargins(0, 0, 0, 0);
    shortcutFieldsLayout->setSpacing(8);

    auto* aiShortcutColumn = new QWidget(shortcutFieldsRow);
    auto* aiShortcutColumnLayout = new QVBoxLayout(aiShortcutColumn);
    aiShortcutColumnLayout->setContentsMargins(0, 0, 0, 0);
    aiShortcutColumnLayout->setSpacing(4);
    auto* aiShortcutLabel = new QLabel(QStringLiteral("AI"), aiShortcutColumn);
    aiShortcutLabel->setObjectName(QStringLiteral("settingsSubtitle"));
    aiShortcutColumnLayout->addWidget(aiShortcutLabel);
    aiShortcutColumnLayout->addWidget(aiShortcutField_);

    auto* screenshotShortcutColumn = new QWidget(shortcutFieldsRow);
    auto* screenshotShortcutColumnLayout = new QVBoxLayout(screenshotShortcutColumn);
    screenshotShortcutColumnLayout->setContentsMargins(0, 0, 0, 0);
    screenshotShortcutColumnLayout->setSpacing(4);
    auto* screenshotShortcutLabel = new QLabel(QStringLiteral("普通截图"), screenshotShortcutColumn);
    screenshotShortcutLabel->setObjectName(QStringLiteral("settingsSubtitle"));
    screenshotShortcutColumnLayout->addWidget(screenshotShortcutLabel);
    screenshotShortcutColumnLayout->addWidget(screenshotShortcutField_);

    shortcutFieldsLayout->addWidget(aiShortcutColumn, 1);
    shortcutFieldsLayout->addWidget(screenshotShortcutColumn, 1);
    providerForm->addRow(QStringLiteral("快捷键"), shortcutFieldsRow);

    auto* appearanceCard = new QFrame(this);
    appearanceCard->setObjectName(QStringLiteral("settingsCard"));
    auto* appearanceLayout = new QVBoxLayout(appearanceCard);
    appearanceLayout->setContentsMargins(12, 12, 12, 12);
    appearanceLayout->setSpacing(10);

    auto* previewShell = new QFrame(appearanceCard);
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
                                        appearanceCard);
    transparencyHint->setObjectName(QStringLiteral("settingsSubtitle"));
    transparencyHint->setWordWrap(true);

    appearanceLayout->addWidget(previewShell);
    appearanceLayout->addLayout(appearanceForm);
    appearanceLayout->addWidget(transparencyHint);

    auto* promptCard = new QFrame(this);
    promptCard->setObjectName(QStringLiteral("settingsCard"));
    auto* promptLayout = new QVBoxLayout(promptCard);
    promptLayout->setContentsMargins(12, 12, 12, 12);
    promptLayout->setSpacing(8);
    auto* promptLabel = new QLabel(QStringLiteral("首轮提示词"), promptCard);
    promptLabel->setObjectName(QStringLiteral("sectionTitle"));
    promptLayout->addWidget(promptLabel);
    promptLayout->addWidget(firstPromptField_);
    promptLayout->addWidget(launchAtLoginCheckBox_);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttonBox->setObjectName(QStringLiteral("settingsButtonBox"));
    if (QPushButton* saveButton = buttonBox->button(QDialogButtonBox::Save); saveButton != nullptr) {
        saveButton->setObjectName(QStringLiteral("settingsSaveButton"));
        connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::saveRequested);
    }
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (QPushButton* cancelButton = buttonBox->button(QDialogButtonBox::Cancel); cancelButton != nullptr) {
        cancelButton->setObjectName(QStringLiteral("settingsCancelButton"));
    }

    auto* scrollContent = new QWidget(this);
    scrollContent->setObjectName(QStringLiteral("settingsScrollContent"));
    auto* scrollContentLayout = new QVBoxLayout(scrollContent);
    scrollContentLayout->setContentsMargins(0, 0, 0, 0);
    scrollContentLayout->setSpacing(10);
    scrollContentLayout->addWidget(headerLabel_);
    scrollContentLayout->addWidget(subtitleLabel_);
    scrollContentLayout->addWidget(providerCard);
    scrollContentLayout->addWidget(appearanceCard);
    scrollContentLayout->addWidget(promptCard);
    scrollContentLayout->addStretch(1);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("settingsScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(scrollContent);
    scrollArea->viewport()->setObjectName(QStringLiteral("settingsScrollViewport"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 8, 10, 8);
    rootLayout->setSpacing(8);
    rootLayout->addWidget(scrollArea, 1);
    rootLayout->addWidget(buttonBox);

    const QSignalBlocker protocolBlocker(protocolSelector_);
    protocolSelector_->setCurrentIndex(indexForProtocol(config.activeProfile.protocol));
    baseUrlField_->setText(config.activeProfile.baseUrl);
    apiKeyField_->setText(config.activeProfile.apiKey);
    modelField_->setCurrentText(config.activeProfile.model);
    if (!config.activeProfile.model.trimmed().isEmpty()) {
        modelField_->addItem(config.activeProfile.model.trimmed());
    }
    aiShortcutField_->setKeySequence(QKeySequence::fromString(config.aiShortcut, QKeySequence::PortableText));
    screenshotShortcutField_->setKeySequence(
        QKeySequence::fromString(config.screenshotShortcut, QKeySequence::PortableText));
    themeField_->setCurrentIndex(qMax(0, themeField_->findData(config.theme)));
    opacityField_->setValue(config.opacity * 100.0);
    firstPromptField_->setPlainText(config.firstPrompt);
    launchAtLoginCheckBox_->setChecked(config.launchAtLogin);

    setPanelColor(QColor(config.panelColor));
    if (const QColor configuredTextColor(config.panelTextColor); configuredTextColor.isValid()) {
        setPanelTextColor(configuredTextColor);
    } else {
        restoreAutomaticTextColor();
    }
    if (const QColor configuredBorderColor(config.panelBorderColor); configuredBorderColor.isValid()) {
        setPanelBorderColor(configuredBorderColor);
    } else {
        restoreAutomaticBorderColor();
    }
    applyAppearance(themeField_->currentData().toString());

    connect(protocolSelector_,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) { handleProtocolChanged(); });
    connect(themeField_,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) {
                applyAppearance(themeField_->currentData().toString());
                if (!panelTextColorCustomized_) {
                    restoreAutomaticTextColor();
                } else if (!panelBorderColorCustomized_) {
                    restoreAutomaticBorderColor();
                }
            });
    connect(opacityField_,
            &QDoubleSpinBox::valueChanged,
            this,
            [this](double) { refreshPreview(); });
    connect(panelColorButton_, &QPushButton::clicked, this, &SettingsDialog::choosePanelColor);
    connect(panelTextColorButton_, &QPushButton::clicked, this, &SettingsDialog::choosePanelTextColor);
    connect(panelTextAutoButton_, &QPushButton::clicked, this, &SettingsDialog::restoreAutomaticTextColor);
    connect(panelBorderColorButton_, &QPushButton::clicked, this, &SettingsDialog::choosePanelBorderColor);
    connect(panelBorderAutoButton_, &QPushButton::clicked, this, &SettingsDialog::restoreAutomaticBorderColor);
    connect(modelPopupButton_, &QToolButton::clicked, this, [this]() {
        if (modelField_ != nullptr) {
            modelField_->setFocus();
            modelField_->showPopup();
        }
    });
    connect(fetchModelsButton_, &QPushButton::clicked, this, &SettingsDialog::fetchModelsRequested);
    connect(testConnectionButton_, &QPushButton::clicked, this, &SettingsDialog::testConnectionRequested);
    connect(testImageButton_,
            &QPushButton::clicked,
            this,
            &SettingsDialog::testImageUnderstandingRequested);
    refreshModelActionUi();
    refreshPreview();
}

ProviderProfile SettingsDialog::currentProfile() const {
    return ProviderProfile{
        .protocol = protocolFromCombo(protocolSelector_),
        .baseUrl = baseUrlField_->text().trimmed(),
        .apiKey = apiKeyField_->text().trimmed(),
        .model = modelField_->currentText().trimmed(),
    };
}

AppConfig SettingsDialog::currentConfig() const {
    AppConfig config = initialConfig_;
    config.activeProfile = currentProfile();
    const QString aiShortcutText = aiShortcutField_->keySequence().toString(QKeySequence::PortableText).trimmed();
    const QString screenshotShortcutText =
        screenshotShortcutField_->keySequence().toString(QKeySequence::PortableText).trimmed();
    config.aiShortcut = aiShortcutText.isEmpty()
        ? QStringLiteral("Ctrl+Shift+A")
        : aiShortcutText;
    config.screenshotShortcut = screenshotShortcutText.isEmpty()
        ? QStringLiteral("Ctrl+Shift+S")
        : screenshotShortcutText;
    config.theme = themeField_->currentData().toString();
    config.opacity = opacityField_->value() / 100.0;
    config.panelColor = serializeColor(panelColor_);
    config.panelTextColor = panelTextColorCustomized_ ? serializeColor(panelTextColor_) : QString();
    config.panelBorderColor = panelBorderColorCustomized_ ? serializeColor(panelBorderColor_) : QString();
    config.launchAtLogin = launchAtLoginCheckBox_ != nullptr && launchAtLoginCheckBox_->isChecked();
    config.firstPrompt = firstPromptField_->toPlainText().trimmed();
    return config;
}

void SettingsDialog::applyAppearance(const QString& theme) {
    setStyleSheet(dialogStyleSheetForTheme(theme));
    applyImmersiveDarkTitleBar(this, effectiveThemeName(theme) == QStringLiteral("dark"));
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialog::applyProtocolPreset(ProviderProtocol protocol) {
    const int targetIndex = indexForProtocol(protocol);
    if (targetIndex >= 0 && protocolSelector_->currentIndex() != targetIndex) {
        protocolSelector_->setCurrentIndex(targetIndex);
        return;
    }

    const ProviderPreset preset = presetFor(protocol);
    baseUrlField_->setText(preset.defaultBaseUrl);
    modelField_->clear();
    modelField_->addItem(preset.defaultModel);
    modelField_->setCurrentText(preset.defaultModel);
    modelField_->setToolTip(preset.modelHint);
    actionMode_ = ActionMode::None;
    if (modelActionStatusLabel_ != nullptr) {
        modelActionStatusLabel_->setText(QStringLiteral("协议已切换，可重新获取模型列表。"));
    }
    refreshModelActionUi();
}

void SettingsDialog::setPanelColor(const QColor& color) {
    panelColor_ = color.isValid()
        ? color
        : fallbackSurfaceColor(themeField_ != nullptr ? themeField_->currentData().toString()
                                                      : QStringLiteral("dark"));
    if (!panelTextColorCustomized_) {
        panelTextColor_ = autoTextColor(panelColor_,
                                        themeField_ != nullptr ? themeField_->currentData().toString()
                                                               : QStringLiteral("dark"));
    }
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_,
                                            themeField_ != nullptr ? themeField_->currentData().toString()
                                                                   : QStringLiteral("dark"));
    }
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialog::setPanelTextColor(const QColor& color) {
    panelTextColorCustomized_ = color.isValid();
    panelTextColor_ = panelTextColorCustomized_
        ? color
        : autoTextColor(panelColor_,
                        themeField_ != nullptr ? themeField_->currentData().toString()
                                               : QStringLiteral("dark"));
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_,
                                            themeField_ != nullptr ? themeField_->currentData().toString()
                                                                   : QStringLiteral("dark"));
    }
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialog::setPanelBorderColor(const QColor& color) {
    panelBorderColorCustomized_ = color.isValid();
    panelBorderColor_ = panelBorderColorCustomized_
        ? color
        : autoBorderColor(panelColor_,
                          themeField_ != nullptr ? themeField_->currentData().toString()
                                                 : QStringLiteral("dark"));
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialog::setAvailableModels(const QStringList& models) {
    const QString currentText = modelField_ != nullptr ? modelField_->currentText().trimmed() : QString();

    QStringList uniqueModels = models;
    uniqueModels.removeDuplicates();
    std::sort(uniqueModels.begin(), uniqueModels.end(),
              [](const QString& left, const QString& right) {
                  return QString::compare(left, right, Qt::CaseInsensitive) < 0;
              });

    if (modelField_ != nullptr) {
        modelField_->clear();
        modelField_->addItems(uniqueModels);
        if (!currentText.isEmpty()) {
            modelField_->setCurrentText(currentText);
        } else if (!uniqueModels.isEmpty()) {
            modelField_->setCurrentIndex(0);
        }
    }

    const QString status = uniqueModels.isEmpty()
        ? QStringLiteral("没有拿到可用模型列表，仍然可以手动输入模型名。")
        : QStringLiteral("已获取 %1 个模型，可直接下拉选择。").arg(uniqueModels.size());
    if (modelActionStatusLabel_ != nullptr) {
        modelActionStatusLabel_->setText(status);
    }
}

void SettingsDialog::setActionMode(const ActionMode mode, const QString& status) {
    actionMode_ = mode;
    if (modelActionStatusLabel_ != nullptr && !status.trimmed().isEmpty()) {
        modelActionStatusLabel_->setText(status.trimmed());
    }
    refreshModelActionUi();
}

void SettingsDialog::setBusy(bool busy, const QString& status) {
    statusLabel_->setText(themedStatusText(busy, status));

    if (!busy) {
        actionMode_ = ActionMode::None;
        if (modelActionStatusLabel_ != nullptr && !status.trimmed().isEmpty()) {
            modelActionStatusLabel_->setText(status.trimmed());
        }
    } else if (modelActionStatusLabel_ != nullptr && !status.trimmed().isEmpty()) {
        modelActionStatusLabel_->setText(status.trimmed());
    }

    for (QWidget* widget : {
             static_cast<QWidget*>(protocolSelector_),
             static_cast<QWidget*>(baseUrlField_),
             static_cast<QWidget*>(apiKeyField_),
             static_cast<QWidget*>(modelField_),
             static_cast<QWidget*>(modelPopupButton_),
             static_cast<QWidget*>(aiShortcutField_),
             static_cast<QWidget*>(screenshotShortcutField_),
             static_cast<QWidget*>(themeField_),
             static_cast<QWidget*>(opacityField_),
             static_cast<QWidget*>(panelColorButton_),
             static_cast<QWidget*>(panelTextColorButton_),
             static_cast<QWidget*>(panelTextAutoButton_),
             static_cast<QWidget*>(panelBorderColorButton_),
             static_cast<QWidget*>(panelBorderAutoButton_),
             static_cast<QWidget*>(firstPromptField_),
             static_cast<QWidget*>(launchAtLoginCheckBox_),
             static_cast<QWidget*>(fetchModelsButton_),
             static_cast<QWidget*>(testConnectionButton_),
             static_cast<QWidget*>(testImageButton_),
         }) {
        if (widget != nullptr) {
            widget->setEnabled(!busy);
        }
    }

    refreshModelActionUi();
}

void SettingsDialog::handleProtocolChanged() {
    applyProtocolPreset(protocolFromCombo(protocolSelector_));
}

void SettingsDialog::choosePanelColor() {
    const QColor originalColor = panelColor_;
    const QColor originalTextColor = panelTextColor_;
    const bool originalCustomized = panelTextColorCustomized_;
    const QColor originalBorderColor = panelBorderColor_;
    const bool originalBorderCustomized = panelBorderColorCustomized_;

    QColorDialog dialog(panelColor_, this);
    dialog.setWindowTitle(QStringLiteral("选择聊天背景色"));
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    connect(&dialog, &QColorDialog::currentColorChanged, this,
            [this](const QColor& current) {
                if (current.isValid()) {
                    setPanelColor(current);
                }
            });

    if (dialog.exec() != QDialog::Accepted) {
        panelColor_ = originalColor;
        panelTextColor_ = originalTextColor;
        panelTextColorCustomized_ = originalCustomized;
        panelBorderColor_ = originalBorderColor;
        panelBorderColorCustomized_ = originalBorderCustomized;
        refreshColorButtons();
        refreshPreview();
        return;
    }

    setPanelColor(dialog.currentColor());
}

void SettingsDialog::choosePanelTextColor() {
    const QColor originalTextColor = panelTextColor_;
    const bool originalCustomized = panelTextColorCustomized_;
    const QColor originalBorderColor = panelBorderColor_;
    const bool originalBorderCustomized = panelBorderColorCustomized_;

    QColorDialog dialog(panelTextColor_, this);
    dialog.setWindowTitle(QStringLiteral("选择字体颜色"));
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    connect(&dialog, &QColorDialog::currentColorChanged, this,
            [this](const QColor& current) {
                if (current.isValid()) {
                    setPanelTextColor(current);
                }
            });

    if (dialog.exec() != QDialog::Accepted) {
        panelTextColor_ = originalTextColor;
        panelTextColorCustomized_ = originalCustomized;
        panelBorderColor_ = originalBorderColor;
        panelBorderColorCustomized_ = originalBorderCustomized;
        refreshColorButtons();
        refreshPreview();
        return;
    }

    setPanelTextColor(dialog.currentColor());
}

void SettingsDialog::choosePanelBorderColor() {
    const QColor originalBorderColor = panelBorderColor_;
    const bool originalBorderCustomized = panelBorderColorCustomized_;

    QColorDialog dialog(panelBorderColor_, this);
    dialog.setWindowTitle(QStringLiteral("选择边框颜色"));
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    connect(&dialog, &QColorDialog::currentColorChanged, this,
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

void SettingsDialog::restoreAutomaticTextColor() {
    panelTextColorCustomized_ = false;
    panelTextColor_ = autoTextColor(panelColor_,
                                    themeField_ != nullptr ? themeField_->currentData().toString()
                                                           : QStringLiteral("dark"));
    if (!panelBorderColorCustomized_) {
        panelBorderColor_ = autoBorderColor(panelColor_,
                                            themeField_ != nullptr ? themeField_->currentData().toString()
                                                                   : QStringLiteral("dark"));
    }
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialog::restoreAutomaticBorderColor() {
    panelBorderColorCustomized_ = false;
    panelBorderColor_ = autoBorderColor(panelColor_,
                                        themeField_ != nullptr ? themeField_->currentData().toString()
                                                               : QStringLiteral("dark"));
    refreshColorButtons();
    refreshPreview();
}

void SettingsDialog::refreshColorButtons() {
    const QString currentTheme = themeField_ != nullptr ? themeField_->currentData().toString()
                                                        : QStringLiteral("dark");
    if (panelColorButton_ != nullptr) {
        const QColor buttonTextColor = autoTextColor(panelColor_, currentTheme);
        panelColorButton_->setText(QStringLiteral("选择颜色  %1").arg(serializeColor(panelColor_).toUpper()));
        panelColorButton_->setStyleSheet(colorButtonStyle(panelColor_, buttonTextColor, currentTheme));
    }

    if (panelTextColorButton_ != nullptr) {
        const QString text = panelTextColorCustomized_
            ? QStringLiteral("选择颜色  %1").arg(serializeColor(panelTextColor_).toUpper())
            : QStringLiteral("自动（当前 %1）").arg(serializeColor(panelTextColor_).toUpper());
        panelTextColorButton_->setText(text);
        panelTextColorButton_->setStyleSheet(colorButtonStyle(panelColor_, panelTextColor_, currentTheme));
    }

    if (panelTextAutoButton_ != nullptr) {
        if (!panelTextColorCustomized_) {
            panelTextAutoButton_->setToolTip(QStringLiteral("当前已使用自动字体颜色"));
        } else {
            panelTextAutoButton_->setToolTip(QStringLiteral("恢复为自动计算的高对比字体色"));
        }
    }

    if (panelBorderColorButton_ != nullptr) {
        const QColor buttonTextColor = autoTextColor(panelBorderColor_, currentTheme);
        const QString text = panelBorderColorCustomized_
            ? QStringLiteral("选择颜色  %1").arg(serializeColor(panelBorderColor_).toUpper())
            : QStringLiteral("自动（当前 %1）").arg(serializeColor(panelBorderColor_).toUpper());
        panelBorderColorButton_->setText(text);
        panelBorderColorButton_->setStyleSheet(
            colorButtonStyle(panelBorderColor_, buttonTextColor, currentTheme));
    }

    if (panelBorderAutoButton_ != nullptr) {
        if (!panelBorderColorCustomized_) {
            panelBorderAutoButton_->setToolTip(QStringLiteral("当前已使用自动边框颜色"));
        } else {
            panelBorderAutoButton_->setToolTip(QStringLiteral("恢复为自动计算的边框颜色"));
        }
    }
}

void SettingsDialog::refreshPreview() {
    if (previewSurface_ == nullptr || previewTitleLabel_ == nullptr || previewStatusLabel_ == nullptr ||
        previewHistoryView_ == nullptr || previewInputField_ == nullptr || previewSendButton_ == nullptr ||
        previewReasoningToggle_ == nullptr) {
        return;
    }

    const double normalizedOpacity = opacityField_ != nullptr ? opacityField_->value() / 100.0 : 1.0;
    QColor effectiveColor = panelColor_;
    effectiveColor.setAlpha(qBound(0, qRound(panelColor_.alphaF() * normalizedOpacity * 255.0), 255));
    const QString currentTheme = themeField_ != nullptr ? themeField_->currentData().toString()
                                                        : QStringLiteral("dark");
    const QColor borderColor = panelBorderColorCustomized_
        ? panelBorderColor_
        : autoBorderColor(panelColor_, currentTheme);
    const QColor mutedColor = mutedTextColorForTheme(currentTheme);
    const QString codeSurface = cssColor(panelColor_, qBound(0, effectiveColor.alpha() + 18, 255));
    const QString chromeSurface = cssColor(panelColor_, qBound(0, effectiveColor.alpha() + 28, 255));
    const QString previewBorderCss = effectiveThemeName(currentTheme) == QStringLiteral("dark")
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
    previewReasoningToggle_->setStyleSheet(QStringLiteral("QToolButton { color: %1; border: none; padding: 0; background: transparent; }")
                                               .arg(mutedColor.name(QColor::HexRgb)));
    previewInputField_->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 16px; padding: 7px 12px; }")
                                          .arg(chromeSurface, panelTextColor_.name(QColor::HexRgb), previewBorderCss));
    previewSendButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 16px; padding: 7px 12px; min-width: 56px; }")
                                           .arg(chromeSurface, panelTextColor_.name(QColor::HexRgb), previewBorderCss));
    previewHistoryView_->document()->setDefaultStyleSheet(
        previewDocumentCss(panelTextColor_.name(QColor::HexRgb),
                           mutedColor.name(QColor::HexRgb),
                           codeSurface,
                           chromeSurface,
                           borderColor.name(QColor::HexRgb)));
    previewHistoryView_->setHtml(previewDocumentHtml(
        previewDocumentCss(panelTextColor_.name(QColor::HexRgb),
                           mutedColor.name(QColor::HexRgb),
                           codeSurface,
                           chromeSurface,
                           borderColor.name(QColor::HexRgb))));
    previewHistoryView_->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: transparent; color: %1; border: none; }")
                                           .arg(panelTextColor_.name(QColor::HexRgb)));
}

void SettingsDialog::refreshModelActionUi() {
    if (fetchModelsButton_ != nullptr) {
        fetchModelsButton_->setText(actionMode_ == ActionMode::FetchModels
                                        ? QStringLiteral("获取中…")
                                        : QStringLiteral("获取模型"));
    }
    if (testConnectionButton_ != nullptr) {
        testConnectionButton_->setText(actionMode_ == ActionMode::TestText
                                           ? QStringLiteral("测试中…")
                                           : QStringLiteral("文字测试"));
    }
    if (testImageButton_ != nullptr) {
        testImageButton_->setText(actionMode_ == ActionMode::TestImage
                                      ? QStringLiteral("测试中…")
                                      : QStringLiteral("图片测试"));
    }
}

int SettingsDialog::indexForProtocol(ProviderProtocol protocol) const {
    return protocolSelector_->findData(static_cast<int>(protocol));
}

}  // namespace ais::ui
