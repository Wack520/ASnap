#include "app/application_controller.h"
#include <memory>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include "ai/ai_client.h"
#include "ai/provider_test_runner.h"
#include "ai/qt_network_transport.h"
#include "app/capture_coordinator.h"
#include "app/conversation_runtime.h"
#include "app/settings_coordinator.h"
#include "app/shell_integration.h"
#include "capture/capture_selection.h"
#include "capture/desktop_capture_service.h"
#include "config/config_store.h"
#include "config/provider_preset.h"
#include "ui/chat/floating_chat_panel.h"
#include "ui/panel_placement.h"

namespace ais::app {
namespace {
[[nodiscard]] QString defaultConfigPath() {
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/.ais_screenshot_tool");
    }
    return QDir(root).filePath(QStringLiteral("config.json"));
}
[[nodiscard]] config::ProviderProfile withDefaults(config::ProviderProfile profile) {
    const auto preset = config::presetFor(profile.protocol);
    if (profile.baseUrl.trimmed().isEmpty()) {
        profile.baseUrl = preset.defaultBaseUrl;
    }
    if (profile.model.trimmed().isEmpty()) {
        profile.model = preset.defaultModel;
    }
    return profile;
}
[[nodiscard]] QScreen* screenForRect(const QRect& rect) {
    if (QScreen* screen = QGuiApplication::screenAt(rect.center()); screen != nullptr) {
        return screen;
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen != nullptr && screen->geometry().contains(rect.center())) {
            return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}
void showAndFocusPanel(ui::FloatingChatPanel* panel) {
    if (panel == nullptr) {
        return;
    }
    panel->show();
    panel->raise();
    panel->activateWindow();
}
}  // namespace

ApplicationController::ApplicationController(QObject* parent) : QObject(parent) {}

ApplicationController::~ApplicationController() {
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    if (shellIntegration_ != nullptr) {
        shellIntegration_->releaseResources();
    }
    if (chatPanel_ != nullptr) {
        chatPanel_->close();
        chatPanel_->deleteLater();
        chatPanel_ = nullptr;
    }
}

bool ApplicationController::initialize() {
    if (initialized_) {
        return true;
    }
    if (qobject_cast<QApplication*>(QCoreApplication::instance()) == nullptr ||
        !QSystemTrayIcon::isSystemTrayAvailable()) {
        return false;
    }
    ensureServiceOwnership();
    loadConfig();
    applyConfigDefaults();
    applyCaptureMode();
    ensureChatPanel();
    if (shellIntegration_ == nullptr || !shellIntegration_->initialize(chatPanel_)) {
        return false;
    }
    if (!shellIntegration_->refreshHotkeys(config_)) {
        shellIntegration_->showMessage(QStringLiteral("全局快捷键注册失败，请检查 AI 快捷键 / 截图快捷键是否冲突"),
                                       QSystemTrayIcon::Warning,
                                       3000);
    }
    (void)shellIntegration_->applyLaunchAtLoginPreference(config_);
    applyAppearance();
    syncBusyUi();
    initialized_ = true;
    return true;
}

bool ApplicationController::canStartCaptureForTest() const {
    return captureCoordinator_ != nullptr ? captureCoordinator_->canStartCaptureForState(guard_.state())
                                          : guard_.state() != BusyState::Capturing &&
                                                guard_.state() != BusyState::TestingProvider;
}

void ApplicationController::closeSettingsDialogForTest() {
    ensureServiceOwnership();
    if (settingsCoordinator_ != nullptr) settingsCoordinator_->closeSettingsDialogForTest();
}

void ApplicationController::completeProviderTestForTest(const bool imageMode,
                                                        const bool success,
                                                        const QString& textOrError) {
    ensureServiceOwnership();
    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->completeProviderTestForTest(imageMode, success, textOrError);
    }
}

void ApplicationController::confirmCaptureForTest(const capture::CaptureSelection& selection,
                                                  const bool sendToAi) {
    ensureServiceOwnership();
    if (captureCoordinator_ != nullptr) {
        captureCoordinator_->confirmCaptureForTest(selection, sendToAi);
    }
}

void ApplicationController::openSettings() {
    ensureServiceOwnership();
    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->openSettings();
    }
}

void ApplicationController::onFollowUpRequested(const QString& text) {
    ensureServiceOwnership();
    if (conversationRuntime_ != nullptr) {
        conversationRuntime_->followUpRequested(text);
    }
}

void ApplicationController::onChatPanelDismissed() {
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    ensureServiceOwnership();
    if (conversationRuntime_ != nullptr) {
        conversationRuntime_->cancelCurrentConversation(true);
    }
    syncBusyUi(QStringLiteral("Ready"));
}

void ApplicationController::quitRequested() {
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    if (shellIntegration_ != nullptr) {
        shellIntegration_->releaseResources();
    }
    QCoreApplication::quit();
}

void ApplicationController::ensureServiceOwnership() {
    if (configStore_ == nullptr) {
        configStore_ = std::make_unique<config::ConfigStore>(defaultConfigPath());
    }
    if (captureService_ == nullptr) {
        captureService_ = std::make_unique<capture::DesktopCaptureService>();
    }
    applyCaptureMode();

    if (aiClient_ == nullptr) {
        aiClient_ = std::make_unique<ai::AiClient>(std::make_unique<ai::QtNetworkTransport>(), guard_);
    }
    if (providerTestRunner_ == nullptr) {
        providerTestRunner_ = std::make_unique<ai::ProviderTestRunner>(std::make_unique<ai::QtNetworkTransport>(),
                                                                        guard_);
    }

    if (shellIntegration_ == nullptr) {
        ShellIntegration::Hooks hooks;
        hooks.onAiCaptureRequested = [this]() { startCapture(); };
        hooks.onPlainCaptureRequested = [this]() { startPlainCapture(); };
        hooks.onSettingsRequested = [this]() { openSettings(); };
        hooks.onQuitRequested = [this]() { quitRequested(); };
        shellIntegration_ = std::make_unique<ShellIntegration>(std::move(hooks), this);
    }

    if (captureCoordinator_ == nullptr) {
        CaptureCoordinator::Hooks hooks;
        hooks.captureDesktop = [this]() { return captureService_->captureVirtualDesktop(); };
        hooks.onAiSelection = [this](const capture::CaptureSelection& selection) {
            guard_.leave(BusyState::Capturing);
            if (conversationRuntime_ != nullptr) {
                conversationRuntime_->beginSessionFromSelection(selection);
            }
        };
        hooks.onPlainSelection = [this](const capture::CaptureSelection& selection) {
            guard_.leave(BusyState::Capturing);
            handlePlainScreenshotCapture(selection);
        };
        hooks.onCancelled = [this]() {
            guard_.leave(BusyState::Capturing);
            if (conversationRuntime_ != nullptr && conversationRuntime_->hasActiveSession()) {
                showAndFocusPanel(chatPanel_);
            }
        };
        hooks.syncStatus = [this](const QString& status) { syncBusyUi(status); };
        captureCoordinator_ = std::make_unique<CaptureCoordinator>(std::move(hooks), this);
    }

    if (settingsCoordinator_ == nullptr) {
        SettingsCoordinator::Hooks hooks;
        hooks.syncStatus = [this](const QString& status) { syncBusyUi(status); };
        hooks.onConfigApplied = [this](const config::AppConfig& updatedConfig) {
            config_ = updatedConfig;
            applyConfigDefaults();
            applyCaptureMode();
        };
        hooks.applyAppearance = [this]() { applyAppearance(); };
        hooks.rememberWindowSizes = [this]() { rememberWindowSizes(); };
        hooks.saveConfigSnapshot = [this]() { return saveConfigSnapshot(); };
        hooks.refreshHotkeys = [this]() {
            return shellIntegration_ != nullptr && shellIntegration_->refreshHotkeys(config_);
        };
        hooks.applyLaunchAtLogin = [this]() {
            return shellIntegration_ != nullptr && shellIntegration_->applyLaunchAtLoginPreference(config_);
        };
        hooks.busyState = [this]() { return guard_.state(); };
        hooks.statusForCurrentState = [this]() { return statusForState(guard_.state()); };
        settingsCoordinator_ = std::make_unique<SettingsCoordinator>(std::move(hooks), this);
    }

    if (conversationRuntime_ == nullptr) {
        ConversationRuntime::Hooks hooks;
        hooks.syncStatus = [this](const QString& status) { syncBusyUi(status); };
        hooks.refreshChatBinding = [this]() {
            if (chatPanel_ != nullptr && conversationRuntime_ != nullptr) {
                chatPanel_->bindSession(conversationRuntime_->currentSession());
            }
        };
        hooks.setChatBusy = [this](const bool busy, const QString& status) {
            if (chatPanel_ != nullptr) {
                chatPanel_->setBusy(busy, status);
            }
        };
        hooks.scheduleSessionRefresh = [this]() {
            if (chatPanel_ != nullptr && conversationRuntime_ != nullptr &&
                conversationRuntime_->hasActiveSession()) {
                chatPanel_->scheduleSessionRefresh();
            }
        };
        hooks.ensureChatPanel = [this]() { ensureChatPanel(); };
        hooks.hasChatPanel = [this]() { return chatPanel_ != nullptr; };
        hooks.showChatPanel = [this]() {
            if (chatPanel_ != nullptr) {
                chatPanel_->show();
            }
        };
        hooks.raiseChatPanel = [this]() {
            if (chatPanel_ != nullptr) {
                chatPanel_->raise();
            }
        };
        hooks.activateChatPanel = [this]() {
            if (chatPanel_ != nullptr) {
                chatPanel_->activateWindow();
            }
        };
        hooks.placeChatPanelNearSelection = [this](const QRect& virtualRect) {
            if (chatPanel_ == nullptr) {
                return;
            }
            if (QScreen* screen = screenForRect(virtualRect); screen != nullptr) {
                chatPanel_->move(ui::choosePanelPosition(virtualRect, chatPanel_->size(), screen->geometry()));
            }
        };
        hooks.busyState = [this]() { return guard_.state(); };
        hooks.isBusy = [this]() { return guard_.isBusy(); };
        hooks.cancelActiveRequest = [this]() {
            if (aiClient_ != nullptr) {
                aiClient_->cancelActiveRequest();
            } else {
                guard_.leave(BusyState::RequestInFlight);
            }
        };
        hooks.statusForCurrentState = [this]() { return statusForState(guard_.state()); };
        hooks.requestStreamStarter = [this](const config::ProviderProfile& profile,
                                            const QList<chat::ChatMessage>& messages,
                                            ai::AiClient::DeltaHandler onTextDelta,
                                            ai::AiClient::DeltaHandler onReasoningDelta,
                                            ai::AiClient::CompletionHandler onComplete,
                                            ai::AiClient::FailureHandler onFailure,
                                            const int retryAttempt) {
            return aiClient_ != nullptr &&
                   aiClient_->sendConversationStream(profile,
                                                     messages,
                                                     std::move(onTextDelta),
                                                     std::move(onReasoningDelta),
                                                     std::move(onComplete),
                                                     std::move(onFailure),
                                                     retryAttempt);
        };
        conversationRuntime_ = std::make_unique<ConversationRuntime>(std::move(hooks), this);
    }

    settingsCoordinator_->setConfig(config_);
    settingsCoordinator_->setProviderTestRunner(providerTestRunner_.get());
    settingsCoordinator_->setDialogParent(chatPanel_);
    conversationRuntime_->setConfig(config_);
}

void ApplicationController::beginCapture(const bool sendToAi) {
    ensureServiceOwnership();
    if (captureCoordinator_ == nullptr || !captureCoordinator_->canStartCaptureForState(guard_.state())) {
        return;
    }
    if (captureCoordinator_->shouldCancelConversationForState(guard_.state()) &&
        conversationRuntime_ != nullptr) {
        conversationRuntime_->cancelCurrentConversation(false);
    }
    if (!guard_.tryEnter(BusyState::Capturing)) {
        return;
    }
    if (chatPanel_ != nullptr) {
        chatPanel_->hide();
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const bool started = sendToAi ? captureCoordinator_->startAiCapture() : captureCoordinator_->startPlainCapture();
    if (!started) {
        guard_.leave(BusyState::Capturing);
        syncBusyUi(lastStatusText_);
    }
}

void ApplicationController::ensureChatPanel() {
    if (chatPanel_ != nullptr) {
        return;
    }
    chatPanel_ = new ui::FloatingChatPanel();
    connect(chatPanel_, &ui::FloatingChatPanel::sendRequested, this, &ApplicationController::onFollowUpRequested);
    connect(chatPanel_, &ui::FloatingChatPanel::panelDismissed, this, &ApplicationController::onChatPanelDismissed);
    connect(chatPanel_, &QObject::destroyed, this, [this]() { chatPanel_ = nullptr; });
    chatPanel_->restoreSavedSize(config_.chatPanelSize);
    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->setDialogParent(chatPanel_);
    }
}

void ApplicationController::ensureSettingsDialog() {
    ensureServiceOwnership();
    ensureChatPanel();
    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->ensureSettingsDialog();
    }
}

void ApplicationController::loadConfig() {
    if (configStore_ != nullptr) config_ = configStore_->load();
}

void ApplicationController::applyConfigDefaults() {
    config_.activeProfile = withDefaults(config_.activeProfile);
    if (config_.aiShortcut.trimmed().isEmpty()) {
        config_.aiShortcut = QStringLiteral("Ctrl+Shift+A");
    }
    if (config_.screenshotShortcut.trimmed().isEmpty()) {
        config_.screenshotShortcut = QStringLiteral("Ctrl+Shift+S");
    }
    if (config_.theme != QStringLiteral("light") &&
        config_.theme != QStringLiteral("dark") &&
        config_.theme != QStringLiteral("system")) {
        config_.theme = QStringLiteral("system");
    }

    config_.opacity = qBound(0.30, config_.opacity, 1.00);
    if (!QColor(config_.panelColor).isValid()) {
        config_.panelColor = QStringLiteral("#101214");
    }
    if (!config_.panelTextColor.trimmed().isEmpty() && !QColor(config_.panelTextColor).isValid()) {
        config_.panelTextColor.clear();
    }
    if (!config_.panelBorderColor.trimmed().isEmpty() && !QColor(config_.panelBorderColor).isValid()) {
        config_.panelBorderColor.clear();
    }
    if (!config_.chatPanelSize.isValid()) {
        config_.chatPanelSize = {};
    }
    if (!config_.settingsDialogSize.isValid()) {
        config_.settingsDialogSize = {};
    }
    if (config_.firstPrompt.trimmed().isEmpty()) {
        config_.firstPrompt = config::defaultFirstPromptText();
    }

    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->setConfig(config_);
    }
    if (conversationRuntime_ != nullptr) {
        conversationRuntime_->setConfig(config_);
    }
}

void ApplicationController::applyCaptureMode() {
    if (captureService_ != nullptr) captureService_->setCaptureMode(config_.captureMode);
}

void ApplicationController::applyAppearance() {
    if (chatPanel_ != nullptr) {
        chatPanel_->applyAppearance(config_.theme,
                                    config_.opacity,
                                    config_.panelColor,
                                    config_.panelTextColor,
                                    config_.panelBorderColor);
    }
    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->applyDialogAppearance(config_.theme);
    }
}

void ApplicationController::handlePlainScreenshotCapture(const capture::CaptureSelection& selection) {
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setPixmap(selection.image);
    }
    syncBusyUi(QStringLiteral("截图已复制到剪贴板"));
    if (shellIntegration_ != nullptr)
        shellIntegration_->showMessage(QStringLiteral("截图已复制到剪贴板"), QSystemTrayIcon::Information, 2000);
    if (conversationRuntime_ != nullptr && conversationRuntime_->hasActiveSession()) {
        showAndFocusPanel(chatPanel_);
    }
}

QString ApplicationController::statusForState(const BusyState state) const {
    switch (state) {
    case BusyState::Idle:
        return QStringLiteral("Ready");
    case BusyState::Capturing:
        return QStringLiteral("Selecting capture area...");
    case BusyState::RequestInFlight:
        return QStringLiteral("Waiting for AI response...");
    case BusyState::TestingProvider:
        return QStringLiteral("Running provider test...");
    }
    return QStringLiteral("Ready");
}

void ApplicationController::rememberWindowSizes() {
    if (chatPanel_ != nullptr) config_.chatPanelSize = chatPanel_->size();
    if (settingsCoordinator_ != nullptr) settingsCoordinator_->rememberDialogSize(&config_);
}

bool ApplicationController::saveConfigSnapshot() const {
    return configStore_ == nullptr || configStore_->save(config_);
}

void ApplicationController::syncBusyUi(const QString& statusOverride) {
    const bool busy = guard_.isBusy();
    const QString status = statusOverride.isEmpty() ? statusForState(guard_.state()) : statusOverride;
    lastStatusText_ = status;
    if (shellIntegration_ != nullptr) {
        shellIntegration_->setAiCaptureEnabled(!busy);
    }
    if (settingsCoordinator_ != nullptr) {
        settingsCoordinator_->setDialogBusy(busy, status);
    }
    if (chatPanel_ != nullptr) {
        chatPanel_->setBusy(busy, status);
    }
}

}  // namespace ais::app
