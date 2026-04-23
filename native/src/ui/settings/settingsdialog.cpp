#include "ui/settings/settingsdialog.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTimer>
#include <QVBoxLayout>

#include "config/provider_preset.h"
#include "ui/settings/settingsdialog_appearance_helpers.h"
#include "ui/settings/settingsdialog_appearance_section.h"

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
using ais::ui::settings_appearance::dialogStyleSheetForTheme;
using ais::ui::settings_appearance::effectiveThemeName;
using ais::ui::settings_appearance::serializeColor;

[[nodiscard]] ProviderProtocol protocolFromCombo(const QComboBox* comboBox) {
    return static_cast<ProviderProtocol>(comboBox->currentData().toInt());
}

[[nodiscard]] QPoint clampTopLeftToVisibleArea(const QPoint& requestedTopLeft,
                                               const QSize& windowSize) {
    const QPoint probePoint = requestedTopLeft + QPoint(24, 24);
    const QRect screenGeometry = [probePoint]() {
        if (QScreen* screen = QGuiApplication::screenAt(probePoint); screen != nullptr) {
            return screen->availableGeometry();
        }
        if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
            return screen->availableGeometry();
        }
        return QRect(QPoint(0, 0), QSize(1920, 1080));
    }();

    const int maxLeft = screenGeometry.left() + qMax(0, screenGeometry.width() - windowSize.width());
    const int maxTop = screenGeometry.top() + qMax(0, screenGeometry.height() - windowSize.height());
    return QPoint(qBound(screenGeometry.left(), requestedTopLeft.x(), maxLeft),
                  qBound(screenGeometry.top(), requestedTopLeft.y(), maxTop));
}

[[nodiscard]] QString themedStatusText(bool busy, const QString& status) {
    if (!status.isEmpty()) {
        return status;
    }
    return busy ? QStringLiteral("处理中…") : QStringLiteral("就绪");
}

void reserveStableStatusHeight(QLabel* label, const int lineCount) {
    if (label == nullptr) {
        return;
    }

    const int reservedHeight = label->fontMetrics().lineSpacing() * qMax(1, lineCount) + 6;
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    label->setMinimumHeight(reservedHeight);
    label->setMaximumHeight(reservedHeight);
}

void reserveStableButtonWidth(QPushButton* button, const QStringList& labels) {
    if (button == nullptr) {
        return;
    }

    int reservedWidth = button->minimumSizeHint().width();
    for (const QString& text : labels) {
        QStyleOptionButton option;
        option.initFrom(button);
        option.text = text;
        const QSize contentSize = button->fontMetrics().size(Qt::TextShowMnemonic, text);
        reservedWidth = qMax(reservedWidth,
                             button->style()->sizeFromContents(QStyle::CT_PushButton,
                                                               &option,
                                                               contentSize,
                                                               button)
                                 .width());
    }

    button->setMinimumWidth(reservedWidth);
    button->setMaximumWidth(reservedWidth);
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
               : QSize(520, 560));
    if (config.settingsDialogPosition.has_value()) {
        move(clampTopLeftToVisibleArea(*config.settingsDialogPosition, size()));
    }
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    headerLabel_ = new QLabel(QStringLiteral("设置"), this);
    headerLabel_->setObjectName(QStringLiteral("settingsHeader"));
    subtitleLabel_ = new QLabel(QStringLiteral("统一管理服务连接、外观和截图后的首轮提示词。"), this);
    subtitleLabel_->setObjectName(QStringLiteral("settingsSubtitle"));
    subtitleLabel_->setWordWrap(true);

    statusLabel_ = new QLabel(QStringLiteral("就绪"), this);
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();

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
    modelField_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    modelField_->setMinimumContentsLength(1);
    modelField_->setMinimumWidth(120);
    modelField_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    if (QLineEdit* lineEdit = modelField_->lineEdit(); lineEdit != nullptr) {
        lineEdit->setClearButtonEnabled(true);
    }

    modelPopupButton_ = new QToolButton(this);
    modelPopupButton_->setObjectName(QStringLiteral("modelPopupButton"));
    modelPopupButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    modelPopupButton_->setArrowType(Qt::DownArrow);
    modelPopupButton_->setToolTip(QStringLiteral("展开模型列表"));
    modelPopupButton_->setCursor(Qt::PointingHandCursor);
    modelPopupButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    modelPopupButton_->setFocusPolicy(Qt::NoFocus);

    fetchModelsButton_ = new QPushButton(QStringLiteral("获取模型"), this);
    fetchModelsButton_->setObjectName(QStringLiteral("modelActionButton"));
    fetchModelsButton_->setMinimumWidth(72);
    fetchModelsButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    fetchModelsButton_->setFocusPolicy(Qt::NoFocus);

    modelActionStatusLabel_ = new QLabel(QStringLiteral("可以先获取模型列表，再做文字 / 图片测试。"), this);
    modelActionStatusLabel_->setObjectName(QStringLiteral("settingsSubtitle"));
    modelActionStatusLabel_->setWordWrap(false);
    reserveStableStatusHeight(modelActionStatusLabel_, 1);

    aiShortcutField_ = new QKeySequenceEdit(this);
    aiShortcutField_->setMaximumSequenceLength(1);

    screenshotShortcutField_ = new QKeySequenceEdit(this);
    screenshotShortcutField_->setMaximumSequenceLength(1);

    captureModeField_ = new QComboBox(this);
    captureModeField_->addItem(QStringLiteral("Standard（优先 WGC）"),
                               static_cast<int>(ais::capture::CaptureMode::Standard));
    captureModeField_->addItem(QStringLiteral("HDR Compatible（优先 GDI）"),
                               static_cast<int>(ais::capture::CaptureMode::HdrCompatible));

    firstPromptField_ = new QPlainTextEdit(this);
    firstPromptField_->setPlaceholderText(QStringLiteral("请输入截图后自动发送给 AI 的首轮提示词"));
    firstPromptField_->setMinimumHeight(96);

    launchAtLoginCheckBox_ = new QCheckBox(QStringLiteral("Windows 当前用户登录后静默启动"), this);

    testConnectionButton_ = new QPushButton(QStringLiteral("测试文字连接"), this);
    testConnectionButton_->setObjectName(QStringLiteral("modelActionButton"));
    testConnectionButton_->setMinimumWidth(72);
    testConnectionButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    testConnectionButton_->setFocusPolicy(Qt::NoFocus);

    testImageButton_ = new QPushButton(QStringLiteral("测试图片理解"), this);
    testImageButton_->setObjectName(QStringLiteral("modelActionButton"));
    testImageButton_->setMinimumWidth(72);
    testImageButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    testImageButton_->setFocusPolicy(Qt::NoFocus);

    reserveStableButtonWidth(fetchModelsButton_,
                             {QStringLiteral("获取模型"), QStringLiteral("获取中…")});
    reserveStableButtonWidth(testConnectionButton_,
                             {QStringLiteral("测试文字连接"),
                              QStringLiteral("文字测试"),
                              QStringLiteral("测试中…")});
    reserveStableButtonWidth(testImageButton_,
                             {QStringLiteral("测试图片理解"),
                              QStringLiteral("图片测试"),
                              QStringLiteral("测试中…")});

    auto* modelToolsRow = new QWidget(this);
    auto* modelToolsLayout = new QHBoxLayout(modelToolsRow);
    modelToolsLayout->setContentsMargins(0, 0, 0, 0);
    modelToolsLayout->setSpacing(4);
    modelToolsLayout->addWidget(modelField_, 1);
    modelToolsLayout->addWidget(modelPopupButton_);
    modelToolsLayout->addWidget(fetchModelsButton_);
    modelToolsLayout->addWidget(testConnectionButton_);
    modelToolsLayout->addWidget(testImageButton_);

    auto* modelActionCard = new QFrame(this);
    modelActionCard->setObjectName(QStringLiteral("settingsCard"));
    modelActionCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* modelToolsStack = new QWidget(modelActionCard);
    auto* modelToolsStackLayout = new QVBoxLayout(modelToolsStack);
    modelToolsStackLayout->setContentsMargins(0, 0, 0, 0);
    modelToolsStackLayout->setSpacing(4);
    modelToolsStackLayout->addWidget(modelToolsRow);
    modelToolsStackLayout->addWidget(modelActionStatusLabel_);

    auto* modelActionCardLayout = new QVBoxLayout(modelActionCard);
    modelActionCardLayout->setContentsMargins(12, 12, 12, 12);
    modelActionCardLayout->setSpacing(0);
    modelActionCardLayout->addWidget(modelToolsStack);

    auto* providerCard = new QFrame(this);
    providerCard->setObjectName(QStringLiteral("settingsCard"));
    auto* providerForm = new QFormLayout(providerCard);
    providerForm->setContentsMargins(12, 12, 12, 12);
    providerForm->setSpacing(8);
    providerForm->setHorizontalSpacing(12);

    providerForm->addRow(QStringLiteral("协议类型"), protocolSelector_);
    providerForm->addRow(QStringLiteral("Base URL"), baseUrlField_);
    providerForm->addRow(QStringLiteral("API Key"), apiKeyField_);
    providerForm->addRow(QStringLiteral("截图模式"), captureModeField_);

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

    appearanceSection_ = new SettingsDialogAppearanceSection(this);

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
    scrollContentLayout->addWidget(providerCard);
    scrollContentLayout->addWidget(appearanceSection_);
    scrollContentLayout->addWidget(promptCard);
    scrollContentLayout->addStretch(1);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("settingsScrollArea"));
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidget(scrollContent);
    scrollArea_->viewport()->setObjectName(QStringLiteral("settingsScrollViewport"));
    if (QScrollBar* verticalScrollBar = scrollArea_->verticalScrollBar(); verticalScrollBar != nullptr) {
        connect(verticalScrollBar, &QScrollBar::valueChanged, this, [this](int) {
            enforceScrollAnchorNow();
        });
        connect(verticalScrollBar, &QScrollBar::rangeChanged, this, [this](int, int) {
            enforceScrollAnchorNow();
        });
    }

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 8, 10, 8);
    rootLayout->setSpacing(8);
    rootLayout->addWidget(headerLabel_);
    rootLayout->addWidget(subtitleLabel_);
    rootLayout->addWidget(modelActionCard);
    rootLayout->addWidget(scrollArea_, 1);
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
    if (captureModeField_ != nullptr) {
        const int captureModeIndex = captureModeField_->findData(static_cast<int>(config.captureMode));
        captureModeField_->setCurrentIndex(qMax(0, captureModeIndex));
    }

    if (QComboBox* themeCombo = themeField(); themeCombo != nullptr) {
        themeCombo->setCurrentIndex(qMax(0, themeCombo->findData(config.theme)));
    }
    if (QDoubleSpinBox* opacitySpin = opacityField(); opacitySpin != nullptr) {
        opacitySpin->setValue(config.opacity * 100.0);
    }

    firstPromptField_->setPlainText(config.firstPrompt);
    launchAtLoginCheckBox_->setChecked(config.launchAtLogin);

    setPanelColor(QColor(config.panelColor));
    if (const QColor configuredTextColor(config.panelTextColor); configuredTextColor.isValid()) {
        setPanelTextColor(configuredTextColor);
    } else if (appearanceSection_ != nullptr) {
        appearanceSection_->restoreAutomaticTextColor();
    }

    if (const QColor configuredBorderColor(config.panelBorderColor); configuredBorderColor.isValid()) {
        setPanelBorderColor(configuredBorderColor);
    } else if (appearanceSection_ != nullptr) {
        appearanceSection_->restoreAutomaticBorderColor();
    }

    applyAppearance(themeField() != nullptr ? themeField()->currentData().toString() : QStringLiteral("dark"));

    connect(protocolSelector_,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) { handleProtocolChanged(); });

    if (QComboBox* themeCombo = themeField(); themeCombo != nullptr) {
        connect(themeCombo,
                &QComboBox::currentIndexChanged,
                this,
                [this](int) {
                    applyAppearance(themeField() != nullptr
                                        ? themeField()->currentData().toString()
                                        : QStringLiteral("dark"));
                    if (appearanceSection_ == nullptr) {
                        return;
                    }
                    if (!appearanceSection_->isPanelTextColorCustomized()) {
                        appearanceSection_->restoreAutomaticTextColor();
                    } else if (!appearanceSection_->isPanelBorderColorCustomized()) {
                        appearanceSection_->restoreAutomaticBorderColor();
                    }
                });
    }

    connect(modelPopupButton_, &QToolButton::clicked, this, [this]() {
        if (modelField_ != nullptr) {
            modelField_->setFocus();
            modelField_->showPopup();
        }
    });
    const auto preserveScrollPosition = [this]() {
        beginScrollAnchorIfNeeded();
    };
    connect(modelPopupButton_, &QToolButton::pressed, this, preserveScrollPosition);
    connect(fetchModelsButton_, &QPushButton::pressed, this, preserveScrollPosition);
    connect(testConnectionButton_, &QPushButton::pressed, this, preserveScrollPosition);
    connect(testImageButton_, &QPushButton::pressed, this, preserveScrollPosition);
    connect(fetchModelsButton_, &QPushButton::clicked, this, &SettingsDialog::fetchModelsRequested);
    connect(testConnectionButton_, &QPushButton::clicked, this, &SettingsDialog::testConnectionRequested);
    connect(testImageButton_,
            &QPushButton::clicked,
            this,
            &SettingsDialog::testImageUnderstandingRequested);

    refreshModelActionUi();
}

QComboBox* SettingsDialog::themeField() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->themeField() : nullptr;
}

QDoubleSpinBox* SettingsDialog::opacityField() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->opacityField() : nullptr;
}

QPushButton* SettingsDialog::panelColorButton() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->panelColorButton() : nullptr;
}

QPushButton* SettingsDialog::panelTextColorButton() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->panelTextColorButton() : nullptr;
}

QPushButton* SettingsDialog::panelTextAutoButton() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->panelTextAutoButton() : nullptr;
}

QPushButton* SettingsDialog::panelBorderColorButton() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->panelBorderColorButton() : nullptr;
}

QPushButton* SettingsDialog::panelBorderAutoButton() const noexcept {
    return appearanceSection_ != nullptr ? appearanceSection_->panelBorderAutoButton() : nullptr;
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
    config.captureMode = captureModeField_ != nullptr
        ? static_cast<ais::capture::CaptureMode>(captureModeField_->currentData().toInt())
        : ais::capture::CaptureMode::Standard;

    config.theme = themeField() != nullptr ? themeField()->currentData().toString() : QStringLiteral("system");
    config.opacity = opacityField() != nullptr ? opacityField()->value() / 100.0 : 1.0;

    if (appearanceSection_ != nullptr) {
        config.panelColor = serializeColor(appearanceSection_->panelColor());
        config.panelTextColor = appearanceSection_->isPanelTextColorCustomized()
            ? serializeColor(appearanceSection_->panelTextColor())
            : QString();
        config.panelBorderColor = appearanceSection_->isPanelBorderColorCustomized()
            ? serializeColor(appearanceSection_->panelBorderColor())
            : QString();
    }

    config.launchAtLogin = launchAtLoginCheckBox_ != nullptr && launchAtLoginCheckBox_->isChecked();
    config.firstPrompt = firstPromptField_->toPlainText().trimmed();
    return config;
}

void SettingsDialog::applyAppearance(const QString& theme) {
    setStyleSheet(dialogStyleSheetForTheme(theme));
    applyImmersiveDarkTitleBar(this, effectiveThemeName(theme) == QStringLiteral("dark"));
    if (appearanceSection_ != nullptr) {
        appearanceSection_->refreshColorButtons();
    }
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
    if (appearanceSection_ != nullptr) {
        appearanceSection_->setPanelColor(color);
    }
}

void SettingsDialog::setPanelTextColor(const QColor& color) {
    if (appearanceSection_ != nullptr) {
        appearanceSection_->setPanelTextColor(color);
    }
}

void SettingsDialog::setPanelBorderColor(const QColor& color) {
    if (appearanceSection_ != nullptr) {
        appearanceSection_->setPanelBorderColor(color);
    }
}

void SettingsDialog::setAvailableModels(const QStringList& models) {
    preserveScrollPosition();

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

    restoreScrollPositionLater();
}

void SettingsDialog::setActionMode(const ActionMode mode, const QString& status) {
    if (mode != ActionMode::None) {
        beginScrollAnchorIfNeeded();
    } else {
        preserveScrollPosition();
    }

    actionMode_ = mode;
    if (modelActionStatusLabel_ != nullptr && !status.trimmed().isEmpty()) {
        modelActionStatusLabel_->setText(status.trimmed());
    }
    refreshModelActionUi();
    restoreScrollPositionLater();
}

void SettingsDialog::setBusy(bool busy, const QString& status) {
    if (busy && actionMode_ != ActionMode::None) {
        beginScrollAnchorIfNeeded();
    } else {
        preserveScrollPosition();
    }

    if (busy) {
        if (QWidget* focusedWidget = focusWidget(); focusedWidget != nullptr) {
            focusedWidget->clearFocus();
        }
        clearFocus();
    }

    statusLabel_->setText(themedStatusText(busy, status));
    statusLabel_->setToolTip(statusLabel_->text());

    if (!busy) {
        actionMode_ = ActionMode::None;
        if (modelActionStatusLabel_ != nullptr && !status.trimmed().isEmpty()) {
            modelActionStatusLabel_->setText(status.trimmed());
        }
    } else if (modelActionStatusLabel_ != nullptr && !status.trimmed().isEmpty()) {
        modelActionStatusLabel_->setText(status.trimmed());
    }
    if (modelActionStatusLabel_ != nullptr) {
        modelActionStatusLabel_->setToolTip(modelActionStatusLabel_->text());
    }

    for (QWidget* widget : {
             static_cast<QWidget*>(protocolSelector_),
             static_cast<QWidget*>(baseUrlField_),
             static_cast<QWidget*>(apiKeyField_),
             static_cast<QWidget*>(modelField_),
             static_cast<QWidget*>(modelPopupButton_),
             static_cast<QWidget*>(captureModeField_),
             static_cast<QWidget*>(aiShortcutField_),
             static_cast<QWidget*>(screenshotShortcutField_),
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

    if (appearanceSection_ != nullptr) {
        appearanceSection_->setBusy(busy);
    }

    refreshModelActionUi();
    restoreScrollPositionLater(!busy);
}

void SettingsDialog::handleProtocolChanged() {
    applyProtocolPreset(protocolFromCombo(protocolSelector_));
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

void SettingsDialog::preserveScrollPosition() {
    if (scrollAnchorActive_) {
        preservedScrollValue_ = scrollAnchorValue_;
        return;
    }

    if (scrollArea_ != nullptr && scrollArea_->verticalScrollBar() != nullptr) {
        preservedScrollValue_ = scrollArea_->verticalScrollBar()->value();
    }
}

void SettingsDialog::beginScrollAnchorIfNeeded() {
    if (!scrollAnchorActive_ && scrollArea_ != nullptr && scrollArea_->verticalScrollBar() != nullptr) {
        scrollAnchorValue_ = scrollArea_->verticalScrollBar()->value();
        scrollAnchorActive_ = true;
    }

    preserveScrollPosition();
    enforceScrollAnchorNow();
}

void SettingsDialog::enforceScrollAnchorNow() {
    if (!scrollAnchorActive_ || scrollArea_ == nullptr || scrollArea_->verticalScrollBar() == nullptr) {
        return;
    }

    QScrollBar* const verticalScrollBar = scrollArea_->verticalScrollBar();
    if (verticalScrollBar->value() == scrollAnchorValue_) {
        return;
    }

    const QSignalBlocker blocker(verticalScrollBar);
    verticalScrollBar->setValue(scrollAnchorValue_);
}

void SettingsDialog::restoreScrollPositionLater(const bool releaseScrollAnchor) {
    if (scrollArea_ == nullptr || scrollArea_->verticalScrollBar() == nullptr) {
        return;
    }

    const int scrollValue = preservedScrollValue_;
    const QList<int> restoreDelays{0, 16, 48, 96};
    for (int index = 0; index < restoreDelays.size(); ++index) {
        const int delayMs = restoreDelays.at(index);
        const bool clearAnchorAfterRestore = releaseScrollAnchor && index == restoreDelays.size() - 1;
        QTimer::singleShot(delayMs, this, [this, scrollValue, clearAnchorAfterRestore]() {
            if (scrollArea_ == nullptr || scrollArea_->verticalScrollBar() == nullptr) {
                return;
            }

            scrollArea_->verticalScrollBar()->setValue(scrollValue);
            if (clearAnchorAfterRestore) {
                scrollAnchorActive_ = false;
            }
        });
    }
}

}  // namespace ais::ui
