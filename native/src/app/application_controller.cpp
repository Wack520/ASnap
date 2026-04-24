#include "app/application_controller.h"

#include <memory>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMenu>
#include <QPixmap>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>

#include "ai/ai_client.h"
#include "ai/provider_test_runner.h"
#include "ai/qt_network_transport.h"
#include "capture/capture_overlay.h"
#include "capture/capture_selection.h"
#include "capture/desktop_capture_service.h"
#include "config/config_store.h"
#include "config/provider_preset.h"
#include "platform/windows/global_hotkey_host.h"
#include "platform/windows/selected_text_query.h"
#include "platform/windows/startup_registry.h"
#include "ui/icon_factory.h"
#include "ui/chat/floating_chat_panel.h"
#include "ui/panel_placement.h"
#include "ui/settings/settingsdialog.h"

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

[[nodiscard]] QString friendlyProviderTestFailure(QString error) {
    error = error.trimmed();
    if (error.contains(QStringLiteral("HTTP 429"), Qt::CaseInsensitive)) {
        return QStringLiteral("服务当前限流（HTTP 429），请稍后再试，或更换模型 / 线路。");
    }

    static const QRegularExpression messagePattern(
        QStringLiteral("\"message\"\\s*:\\s*\"([^\"]+)\""));
    const QRegularExpressionMatch match = messagePattern.match(error);
    if (match.hasMatch()) {
        const QString upstreamMessage = match.captured(1).trimmed();
        if (!upstreamMessage.isEmpty()) {
            return upstreamMessage;
        }
    }

    return error;
}

[[nodiscard]] QString selectedTextPrompt(QString prompt, const QString& text) {
    prompt = prompt.trimmed();
    while (prompt.endsWith(QChar::fromLatin1(':')) ||
           prompt.endsWith(QChar(0xff1a))) {
        prompt.chop(1);
        prompt = prompt.trimmed();
    }
    return QStringLiteral("%1：\n\n%2").arg(prompt, text);
}

}  // namespace

ApplicationController::ApplicationController(QObject* parent)
    : QObject(parent) {
    ConversationRequestController::Hooks hooks;
    hooks.activeProfileProvider = [this]() { return config_.activeProfile; };
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
    hooks.cancelActiveRequest = [this]() {
        if (aiClient_ != nullptr) {
            aiClient_->cancelActiveRequest();
            return;
        }
        guard_.leave(BusyState::RequestInFlight);
    };
    hooks.bindSession = [this](const std::shared_ptr<chat::ChatSession>& session) {
        if (chatPanel_ != nullptr) {
            chatPanel_->bindSession(session);
        }
    };
    hooks.scheduleSessionRefresh = [this]() {
        if (chatPanel_ != nullptr) {
            chatPanel_->scheduleSessionRefresh();
        }
    };
    hooks.setPanelBusy = [this](const bool busy, const QString& status) {
        if (chatPanel_ != nullptr) {
            chatPanel_->setBusy(busy, status);
        }
    };
    hooks.syncStatus = [this](const QString& status) {
        syncBusyUi(status);
    };
    hooks.statusForState = [this](const BusyState state) {
        return statusForState(state);
    };
    requestController_ = std::make_unique<ConversationRequestController>(guard_, std::move(hooks), this);
}

ApplicationController::~ApplicationController() {
    requestController_.reset();
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    clearOverlay();

    if (aiHotkeyHost_ != nullptr) {
        aiHotkeyHost_->unregisterHotkey();
    }
    if (textQueryHotkeyHost_ != nullptr) {
        textQueryHotkeyHost_->unregisterHotkey();
    }
    if (screenshotHotkeyHost_ != nullptr) {
        screenshotHotkeyHost_->unregisterHotkey();
    }

    if (trayIcon_ != nullptr) {
        trayIcon_->hide();
    }

    if (settingsDialog_ != nullptr) {
        settingsDialog_->close();
        settingsDialog_->deleteLater();
        settingsDialog_ = nullptr;
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

    if (qobject_cast<QApplication*>(QCoreApplication::instance()) == nullptr) {
        return false;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return false;
    }

    ensureServiceOwnership();
    loadConfig();
    applyConfigDefaults();
    applyCaptureModeToService();
    (void)applyLaunchAtLoginPreference();
    ensureChatPanel();
    createTray();

    aiHotkeyHost_ = new platform::windows::GlobalHotkeyHost(1, this);
    textQueryHotkeyHost_ = new platform::windows::GlobalHotkeyHost(2, this);
    screenshotHotkeyHost_ = new platform::windows::GlobalHotkeyHost(3, this);
    connect(aiHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
            this, &ApplicationController::startCapture);
    connect(textQueryHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
            this, &ApplicationController::startTextQuery);
    connect(screenshotHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
            this, &ApplicationController::startPlainCapture);

    if (!registerHotkeys() && trayIcon_ != nullptr) {
        trayIcon_->showMessage(
            ui::brandDisplayName(),
            QStringLiteral("全局快捷键注册失败，请检查 文本直查 / AI 截图 / 普通截图 是否冲突"),
            QSystemTrayIcon::Warning,
            3000);
    }

    applyAppearance();
    syncBusyUi();

    initialized_ = true;
    return true;
}

void ApplicationController::forceBusyStateForTest(const BusyState state) {
    guard_.leave(BusyState::Capturing);
    guard_.leave(BusyState::RequestInFlight);
    guard_.leave(BusyState::TestingProvider);

    if (state != BusyState::Idle) {
        (void)guard_.tryEnter(state);
    }
}

bool ApplicationController::canStartCaptureForTest() const {
    return guard_.state() != BusyState::Capturing &&
           guard_.state() != BusyState::TestingProvider;
}

void ApplicationController::loadConfigForTest(const config::AppConfig& config) {
    ensureServiceOwnership();
    config_ = config;
    applyConfigDefaults();
    applyCaptureModeToService();
}

void ApplicationController::ensureSettingsDialogForTest() {
    ensureSettingsDialog();
}

void ApplicationController::ensureChatPanelForTest() {
    ensureChatPanel();
}

void ApplicationController::closeSettingsDialogForTest() {
    if (settingsDialog_ == nullptr) {
        return;
    }

    settingsDialog_->close();
    settingsDialog_->deleteLater();
    settingsDialog_ = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void ApplicationController::closeChatPanelForTest() {
    if (chatPanel_ == nullptr) {
        return;
    }

    chatPanel_->close();
    QCoreApplication::processEvents();
}

capture::CaptureMode ApplicationController::captureModeForTest() const {
    if (captureService_ != nullptr) {
        return captureService_->captureMode();
    }

    return config_.captureMode;
}

void ApplicationController::completeProviderTestForTest(const bool imageMode,
                                                        const bool success,
                                                        const QString& textOrError) {
    if (success) {
        handleProviderTestSuccess(imageMode, textOrError);
        return;
    }

    handleProviderTestFailure(textOrError);
}

void ApplicationController::setRequestStreamStarterForTest(RequestStreamStarter starter) {
    if (requestController_ != nullptr) {
        requestController_->setRequestStreamStarterForTest(std::move(starter));
    }
}

bool ApplicationController::isChatPanelVisibleForTest() const noexcept {
    return chatPanel_ != nullptr && chatPanel_->isVisible();
}

void ApplicationController::setEmptyRetryDelayOverrideForTest(const int delayMs) {
    if (requestController_ != nullptr) {
        requestController_->setEmptyRetryDelayOverrideForTest(delayMs);
    }
}

int ApplicationController::emptyRetryDelayMsForTest(const bool hasImageContext,
                                                    const int emptyRetryAttempt) const {
    if (requestController_ == nullptr) {
        return -1;
    }
    return requestController_->emptyRetryDelayMsForTest(hasImageContext, emptyRetryAttempt);
}

void ApplicationController::seedConversationForTest(const QString& initialUserText) {
    if (requestController_ == nullptr) {
        return;
    }

    ensureChatPanel();
    requestController_->beginSession(QByteArray("png-image"), initialUserText);
    if (chatPanel_ != nullptr) {
        chatPanel_->setBusy(false, QStringLiteral("Ready"));
    }
    syncBusyUi(QStringLiteral("Ready"));
}

void ApplicationController::followUpRequestedForTest(const QString& text) {
    onFollowUpRequested(text);
}

int ApplicationController::queuedFollowUpCountForTest() const noexcept {
    if (requestController_ == nullptr) {
        return 0;
    }
    return requestController_->queuedFollowUpCountForTest();
}

QString ApplicationController::queuedFollowUpTextForTest(const int index) const {
    if (requestController_ == nullptr) {
        return {};
    }
    return requestController_->queuedFollowUpTextForTest(index);
}

int ApplicationController::messageCountForTest() const {
    if (requestController_ == nullptr) {
        return 0;
    }
    return requestController_->messageCountForTest();
}

QString ApplicationController::lastUserMessageTextForTest() const {
    if (requestController_ == nullptr) {
        return {};
    }
    return requestController_->lastUserMessageTextForTest();
}

QString ApplicationController::lastAssistantMessageTextForTest() const {
    if (requestController_ == nullptr) {
        return {};
    }
    return requestController_->lastAssistantMessageTextForTest();
}

QString ApplicationController::lastAssistantReasoningForTest() const {
    if (requestController_ == nullptr) {
        return {};
    }
    return requestController_->lastAssistantReasoningForTest();
}

void ApplicationController::querySelectedTextForTest() {
    startTextQueryWorkflow();
}

void ApplicationController::confirmCaptureForTest(const capture::CaptureSelection& selection, const bool sendToAi) {
    activeCaptureMode_ = sendToAi ? CaptureLaunchMode::AiAssistant : CaptureLaunchMode::PlainScreenshot;
    handleConfirmedCapture(selection);
}

void ApplicationController::startTextQuery() {
    startTextQueryWorkflow();
}

void ApplicationController::startCapture() {
    startCaptureWorkflow(CaptureLaunchMode::AiAssistant);
}

void ApplicationController::startPlainCapture() {
    startCaptureWorkflow(CaptureLaunchMode::PlainScreenshot);
}

void ApplicationController::startCaptureWorkflow(const CaptureLaunchMode mode) {
    if (guard_.state() == BusyState::TestingProvider || guard_.state() == BusyState::Capturing) {
        return;
    }

    if (guard_.state() == BusyState::RequestInFlight) {
        cancelCurrentConversation(false);
    }

    if (!guard_.tryEnter(BusyState::Capturing)) {
        return;
    }

    activeCaptureMode_ = mode;
    syncBusyUi(mode == CaptureLaunchMode::AiAssistant
                   ? QStringLiteral("Select an area to capture...")
                   : QStringLiteral("选择截图区域…"));

    if (chatPanel_ != nullptr) {
        chatPanel_->hide();
    }

    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (captureService_ == nullptr) {
        guard_.leave(BusyState::Capturing);
        syncBusyUi(QStringLiteral("Capture service is unavailable"));
        return;
    }

    const capture::DesktopSnapshot snapshot = captureService_->captureVirtualDesktop();
    if (snapshot.displayImage.isNull() || snapshot.captureImage.isNull() ||
        !snapshot.virtualGeometry.isValid() || snapshot.virtualGeometry.isEmpty()) {
        guard_.leave(BusyState::Capturing);
        syncBusyUi(QStringLiteral("Failed to capture desktop"));
        return;
    }

    clearOverlay();

    const QList<capture::ScreenMapping> screenMappings =
        snapshot.screenMappings.isEmpty()
            ? QList<capture::ScreenMapping>{
                  capture::ScreenMapping{
                      .overlayRect = snapshot.overlayGeometry.isValid()
                                         ? snapshot.overlayGeometry
                                         : snapshot.virtualGeometry,
                      .virtualRect = snapshot.virtualGeometry,
                      .devicePixelRatio = 1.0,
                  }}
            : snapshot.screenMappings;

    capture::CaptureOverlay* primaryOverlay = nullptr;
    for (const capture::ScreenMapping& mapping : screenMappings) {
        const capture::DesktopSnapshot screenSnapshot =
            capture::DesktopCaptureService::snapshotForScreen(snapshot, mapping);
        if (screenSnapshot.displayImage.isNull() || screenSnapshot.captureImage.isNull()) {
            continue;
        }

        auto* overlay = new capture::CaptureOverlay(screenSnapshot);
        connect(overlay, &capture::CaptureOverlay::captureConfirmed,
                this, &ApplicationController::onCaptureConfirmed);
        connect(overlay, &capture::CaptureOverlay::captureCancelled,
                this, &ApplicationController::onCaptureCancelled);
        connect(overlay, &QObject::destroyed, this, [this, overlay]() { overlays_.removeAll(overlay); });

        overlays_.append(overlay);
        if (primaryOverlay == nullptr) {
            primaryOverlay = overlay;
        }

        overlay->show();
        overlay->raise();
    }

    if (primaryOverlay == nullptr) {
        guard_.leave(BusyState::Capturing);
        syncBusyUi(QStringLiteral("Failed to capture desktop"));
        return;
    }

    primaryOverlay->activateWindow();
}

void ApplicationController::openSettings() {
    ensureSettingsDialog();

    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
    syncBusyUi();
}

void ApplicationController::startTextQueryWorkflow() {
    if (guard_.state() == BusyState::TestingProvider || guard_.state() == BusyState::Capturing) {
        return;
    }

    if (guard_.state() == BusyState::RequestInFlight) {
        cancelCurrentConversation(false);
    }

    QString errorMessage;
    const QString selectedText = [this, &errorMessage]() {
        if (selectedTextReaderForTest_) {
            return selectedTextReaderForTest_().trimmed();
        }
        return platform::windows::querySelectedText(&errorMessage).trimmed();
    }();

    if (selectedText.isEmpty()) {
        const QString status = QStringLiteral("未检测到可查询文本，请改用截图查询");
        syncBusyUi(status);
        if (trayIcon_ != nullptr) {
            trayIcon_->showMessage(ui::brandDisplayName(),
                                   status,
                                   QSystemTrayIcon::Information,
                                   2000);
        }
        return;
    }

    beginSessionFromSelectedText(selectedText);
}

void ApplicationController::onCaptureConfirmed(const capture::CaptureSelection& selection) {
    clearOverlay();
    guard_.leave(BusyState::Capturing);
    handleConfirmedCapture(selection);
}

void ApplicationController::onCaptureCancelled() {
    clearOverlay();
    guard_.leave(BusyState::Capturing);
    syncBusyUi(QStringLiteral("Capture cancelled"));

    if (chatPanel_ != nullptr &&
        requestController_ != nullptr &&
        requestController_->hasSession()) {
        chatPanel_->show();
        chatPanel_->raise();
        chatPanel_->activateWindow();
    }
}

void ApplicationController::onFollowUpRequested(const QString& text) {
    if (requestController_ != nullptr) {
        requestController_->onFollowUpRequested(text);
    }
}

void ApplicationController::onSettingsSaveRequested() {
    applySettingsFromDialog();
}

void ApplicationController::onSettingsFetchModelsRequested() {
    fetchProviderModels();
}

void ApplicationController::onSettingsTextTestRequested() {
    runProviderTest(false);
}

void ApplicationController::onSettingsImageTestRequested() {
    runProviderTest(true);
}

void ApplicationController::onSettingsDialogFinished(int result) {
    if (settingsDialog_ == nullptr) {
        return;
    }

    if (result == QDialog::Accepted) {
        applySettingsFromDialog();
        return;
    }

    rememberWindowSizes();
    (void)saveConfigSnapshot();
    syncBusyUi();
}

void ApplicationController::onChatPanelDismissed() {
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    cancelCurrentConversation(true);
    syncBusyUi(QStringLiteral("Ready"));
}

void ApplicationController::quitRequested() {
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    if (aiHotkeyHost_ != nullptr) {
        aiHotkeyHost_->unregisterHotkey();
    }
    if (textQueryHotkeyHost_ != nullptr) {
        textQueryHotkeyHost_->unregisterHotkey();
    }
    if (screenshotHotkeyHost_ != nullptr) {
        screenshotHotkeyHost_->unregisterHotkey();
    }

    if (trayIcon_ != nullptr) {
        trayIcon_->hide();
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
    if (captureService_ != nullptr) {
        captureService_->setCaptureMode(config_.captureMode);
    }

    if (aiClient_ == nullptr) {
        aiClient_ = std::make_unique<ai::AiClient>(
            std::make_unique<ai::QtNetworkTransport>(),
            guard_);
    }

    if (providerTestRunner_ == nullptr) {
        providerTestRunner_ = std::make_unique<ai::ProviderTestRunner>(
            std::make_unique<ai::QtNetworkTransport>(),
            guard_);
    }

    if (startupRegistry_ == nullptr) {
        startupRegistry_ = std::make_unique<platform::windows::WindowsStartupRegistry>();
    }
}

void ApplicationController::ensureChatPanel() {
    if (chatPanel_ != nullptr) {
        return;
    }

    chatPanel_ = new ui::FloatingChatPanel();
    connect(chatPanel_, &ui::FloatingChatPanel::sendRequested,
            this, &ApplicationController::onFollowUpRequested);
    connect(chatPanel_, &ui::FloatingChatPanel::panelDismissed,
            this, &ApplicationController::onChatPanelDismissed);
    connect(chatPanel_, &QObject::destroyed, this, [this]() {
        chatPanel_ = nullptr;
        settingsDialog_ = nullptr;
    });
    chatPanel_->restoreSavedSize(config_.chatPanelSize);
}

void ApplicationController::ensureSettingsDialog() {
    ensureServiceOwnership();
    ensureChatPanel();
    if (settingsDialog_ != nullptr) {
        return;
    }

    settingsDialog_ = new ui::SettingsDialog(config_, chatPanel_);
    settingsDialog_->setModal(false);
    connect(settingsDialog_, &ui::SettingsDialog::saveRequested,
            this, &ApplicationController::onSettingsSaveRequested);
    connect(settingsDialog_, &ui::SettingsDialog::fetchModelsRequested,
            this, &ApplicationController::onSettingsFetchModelsRequested);
    connect(settingsDialog_, &ui::SettingsDialog::testConnectionRequested,
            this, &ApplicationController::onSettingsTextTestRequested);
    connect(settingsDialog_, &ui::SettingsDialog::testImageUnderstandingRequested,
            this, &ApplicationController::onSettingsImageTestRequested);
    connect(settingsDialog_, &QDialog::finished,
            this, &ApplicationController::onSettingsDialogFinished);
    connect(settingsDialog_, &QObject::destroyed, this, [this]() { settingsDialog_ = nullptr; });
}

void ApplicationController::createTray() {
    if (trayIcon_ != nullptr) {
        return;
    }

    trayIcon_ = new QSystemTrayIcon(this);
    trayMenu_ = new QMenu(chatPanel_);
    textQueryAction_ = trayMenu_->addAction(QStringLiteral("文本直查"));
    captureAction_ = trayMenu_->addAction(QStringLiteral("AI 截图"));
    screenshotAction_ = trayMenu_->addAction(QStringLiteral("普通截图"));
    settingsAction_ = trayMenu_->addAction(QStringLiteral("设置"));
    trayMenu_->addSeparator();
    quitAction_ = trayMenu_->addAction(QStringLiteral("退出"));

    connect(textQueryAction_, &QAction::triggered, this, &ApplicationController::startTextQuery);
    connect(captureAction_, &QAction::triggered, this, &ApplicationController::startCapture);
    connect(screenshotAction_, &QAction::triggered, this, &ApplicationController::startPlainCapture);
    connect(settingsAction_, &QAction::triggered, this, &ApplicationController::openSettings);
    connect(quitAction_, &QAction::triggered, this, &ApplicationController::quitRequested);

    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->setIcon(ui::createAppIcon());
    trayIcon_->setToolTip(ui::brandDisplayName());

    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    startCapture();
                }
            });

    trayIcon_->show();
}

void ApplicationController::loadConfig() {
    if (configStore_ != nullptr) {
        config_ = configStore_->load();
    }
}

void ApplicationController::applyConfigDefaults() {
    config_.activeProfile = withDefaults(config_.activeProfile);
    config::normalizeShortcutAssignments(config_);

    if (config_.theme != QStringLiteral("light") &&
        config_.theme != QStringLiteral("dark") &&
        config_.theme != QStringLiteral("system")) {
        config_.theme = QStringLiteral("system");
    }

    config_.opacity = qBound(0.30, config_.opacity, 1.00);
    if (!QColor(config_.panelColor).isValid()) {
        config_.panelColor = QStringLiteral("#ffffff");
    }
    if (!config_.panelTextColor.trimmed().isEmpty() && !QColor(config_.panelTextColor).isValid()) {
        config_.panelTextColor.clear();
    }
    if (config_.panelBorderColor.trimmed().isEmpty() ||
        !QColor(config_.panelBorderColor).isValid()) {
        config_.panelBorderColor = QStringLiteral("#000000");
    }
    if (!config_.chatPanelSize.isValid()) {
        config_.chatPanelSize = config::defaultChatPanelSize();
    }
    if (!config_.settingsDialogSize.isValid()) {
        config_.settingsDialogSize = config::defaultSettingsDialogSize();
    }
    if (config_.firstPrompt.trimmed().isEmpty()) {
        config_.firstPrompt = config::defaultFirstPromptText();
    }
    if (config_.textQueryPrompt.trimmed().isEmpty()) {
        config_.textQueryPrompt = config::defaultTextQueryPromptText();
    }
}

void ApplicationController::applyCaptureModeToService() {
    if (captureService_ != nullptr) {
        captureService_->setCaptureMode(config_.captureMode);
    }
}

void ApplicationController::applyAppearance() {
    if (chatPanel_ != nullptr) {
        chatPanel_->applyAppearance(config_.theme,
                                    config_.opacity,
                                    config_.panelColor,
                                    config_.panelTextColor,
                                    config_.panelBorderColor);
    }
    if (settingsDialog_ != nullptr) {
        settingsDialog_->applyAppearance(config_.theme);
    }
}

bool ApplicationController::registerHotkeys() {
    if (aiHotkeyHost_ == nullptr ||
        textQueryHotkeyHost_ == nullptr ||
        screenshotHotkeyHost_ == nullptr) {
        return false;
    }

    const bool textQueryRegistered = textQueryHotkeyHost_->registerHotkey(config_.textQueryShortcut);
    const bool aiRegistered = aiHotkeyHost_->registerHotkey(config_.aiShortcut);
    const bool screenshotRegistered = screenshotHotkeyHost_->registerHotkey(config_.screenshotShortcut);
    return textQueryRegistered && aiRegistered && screenshotRegistered;
}

void ApplicationController::clearOverlay() {
    if (overlays_.isEmpty()) {
        return;
    }

    const QList<capture::CaptureOverlay*> overlays = overlays_;
    overlays_.clear();
    for (capture::CaptureOverlay* overlay : overlays) {
        if (overlay == nullptr) {
            continue;
        }

        overlay->hide();
        overlay->deleteLater();
    }
}

void ApplicationController::cancelCurrentConversation(const bool clearSession) {
    if (requestController_ != nullptr) {
        requestController_->cancelCurrentConversation(clearSession);
    }
}

void ApplicationController::handleConfirmedCapture(const capture::CaptureSelection& selection) {
    if (activeCaptureMode_ == CaptureLaunchMode::PlainScreenshot) {
        handlePlainScreenshotCapture(selection);
        return;
    }

    beginSessionFromSelection(selection);
}

void ApplicationController::handlePlainScreenshotCapture(const capture::CaptureSelection& selection) {
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setPixmap(selection.image);
    }

    syncBusyUi(QStringLiteral("截图已复制到剪贴板"));
    if (trayIcon_ != nullptr) {
        trayIcon_->showMessage(ui::brandDisplayName(),
                               QStringLiteral("截图已复制到剪贴板"),
                               QSystemTrayIcon::Information,
                               2000);
    }

    if (chatPanel_ != nullptr &&
        requestController_ != nullptr &&
        requestController_->hasSession()) {
        chatPanel_->show();
        chatPanel_->raise();
        chatPanel_->activateWindow();
    }
}

void ApplicationController::beginSessionFromSelection(const capture::CaptureSelection& selection) {
    ensureChatPanel();
    if (chatPanel_ == nullptr) {
        syncBusyUi(QStringLiteral("Chat panel is unavailable"));
        return;
    }

    if (requestController_ == nullptr) {
        syncBusyUi(QStringLiteral("Request controller is unavailable"));
        return;
    }

    chatPanel_->show();

    if (QScreen* screen = screenForRect(selection.virtualRect); screen != nullptr) {
        const QPoint panelPos = ui::choosePanelPosition(
            selection.virtualRect,
            chatPanel_->size(),
            screen->geometry());
        chatPanel_->move(panelPos);
    }

    chatPanel_->raise();
    chatPanel_->activateWindow();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const QPixmap selectionImage = selection.image;
    QTimer::singleShot(0, this, [this, selectionImage]() {
        if (chatPanel_ == nullptr || !chatPanel_->isVisible() || requestController_ == nullptr) {
            return;
        }

        requestController_->beginSession(encodePng(selectionImage), defaultFirstPrompt());

        if (!requestController_->startCurrentSessionRequest(QStringLiteral("Analyzing screenshot..."))) {
            chatPanel_->bindSession(requestController_->session());
            syncBusyUi(QStringLiteral("Unable to start AI request"));
        }
    });
}

void ApplicationController::beginSessionFromSelectedText(const QString& text) {
    ensureChatPanel();
    if (chatPanel_ == nullptr) {
        syncBusyUi(QStringLiteral("Chat panel is unavailable"));
        return;
    }

    if (requestController_ == nullptr) {
        syncBusyUi(QStringLiteral("Request controller is unavailable"));
        return;
    }

    chatPanel_->show();
    chatPanel_->raise();
    chatPanel_->activateWindow();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const QString prompt = selectedTextPrompt(defaultTextQueryPrompt(), text);
    QTimer::singleShot(0, this, [this, prompt]() {
        if (chatPanel_ == nullptr || !chatPanel_->isVisible() || requestController_ == nullptr) {
            return;
        }

        requestController_->beginTextSession(prompt);

        if (!requestController_->startCurrentSessionRequest(QStringLiteral("正在查询选中文本…"))) {
            chatPanel_->bindSession(requestController_->session());
            syncBusyUi(QStringLiteral("Unable to start AI request"));
        }
    });
}

void ApplicationController::runProviderTest(const bool imageMode) {
    if (settingsDialog_ == nullptr || providerTestRunner_ == nullptr) {
        return;
    }
    if (guard_.state() == BusyState::TestingProvider) {
        return;
    }

    const QString runningStatus =
        imageMode
            ? QStringLiteral("正在测试图片理解…")
            : QStringLiteral("正在测试文字连接…");

    settingsDialog_->setActionMode(
        imageMode ? ui::SettingsDialog::ActionMode::TestImage : ui::SettingsDialog::ActionMode::TestText,
        runningStatus);
    settingsDialog_->setBusy(true, runningStatus);

    auto onSuccess = [this, imageMode](QString text) {
        QTimer::singleShot(0, this, [this, imageMode, text = std::move(text)]() {
            handleProviderTestSuccess(imageMode, text);
        });
    };

    auto onFailure = [this](QString error) {
        QTimer::singleShot(0, this, [this, error = std::move(error)]() {
            handleProviderTestFailure(error);
        });
    };

    const config::ProviderProfile profile = withDefaults(settingsDialog_->currentProfile());
    const bool started = imageMode
        ? providerTestRunner_->runImageTest(profile, std::move(onSuccess), std::move(onFailure))
        : providerTestRunner_->runTextTest(profile, std::move(onSuccess), std::move(onFailure));

    if (!started) {
        syncBusyUi(statusForState(guard_.state()));
    } else {
        syncBusyUi(runningStatus);
    }
}

void ApplicationController::fetchProviderModels() {
    if (settingsDialog_ == nullptr || providerTestRunner_ == nullptr) {
        return;
    }
    if (guard_.state() == BusyState::TestingProvider) {
        return;
    }

    const QString runningStatus = QStringLiteral("正在获取模型列表…");
    settingsDialog_->setActionMode(ui::SettingsDialog::ActionMode::FetchModels, runningStatus);
    settingsDialog_->setBusy(true, runningStatus);

    const config::ProviderProfile profile = withDefaults(settingsDialog_->currentProfile());
    const bool started = providerTestRunner_->fetchModels(
        profile,
        [this](QStringList models) {
            QTimer::singleShot(0, this, [this, models = std::move(models)]() {
                if (settingsDialog_ != nullptr) {
                    settingsDialog_->setAvailableModels(models);
                }

                const QString status = models.isEmpty()
                    ? QStringLiteral("模型列表为空，可手动输入模型名。")
                    : QStringLiteral("已获取 %1 个模型，可直接选择。").arg(models.size());
                syncBusyUi(status);
            });
        },
        [this](QString error) {
            QTimer::singleShot(0, this, [this, error = std::move(error)]() {
                handleProviderTestFailure(error);
            });
        });

    if (!started) {
        syncBusyUi(statusForState(guard_.state()));
    } else {
        syncBusyUi(runningStatus);
    }
}

void ApplicationController::handleProviderTestSuccess(const bool imageMode, const QString& text) {
    const QString prefix = imageMode
        ? QStringLiteral("图片理解测试通过")
        : QStringLiteral("文字连接测试通过");
    const QString status = text.trimmed().isEmpty()
        ? prefix
        : QStringLiteral("%1: %2").arg(prefix, text.trimmed());

    syncBusyUi(status);
}

void ApplicationController::handleProviderTestFailure(const QString& error) {
    syncBusyUi(QStringLiteral("测试失败：%1").arg(friendlyProviderTestFailure(error)));
}

void ApplicationController::applySettingsFromDialog() {
    if (settingsDialog_ == nullptr) {
        return;
    }

    config_ = settingsDialog_->currentConfig();
    applyConfigDefaults();
    applyCaptureModeToService();
    rememberWindowSizes();

    const bool saved = saveConfigSnapshot();
    const bool hotkeyRegistered = registerHotkeys();
    const bool launchPreferenceApplied = applyLaunchAtLoginPreference();
    applyAppearance();

    if (!saved) {
        syncBusyUi(QStringLiteral("Settings could not be saved"));
    } else if (!launchPreferenceApplied && !hotkeyRegistered) {
        syncBusyUi(QStringLiteral("Settings saved, but startup and hotkey registration failed"));
    } else if (!launchPreferenceApplied) {
        syncBusyUi(QStringLiteral("Settings saved, but startup registration failed"));
    } else if (!hotkeyRegistered) {
        syncBusyUi(QStringLiteral("Settings saved, but hotkey registration failed"));
    } else {
        syncBusyUi(QStringLiteral("Settings saved"));
    }
}

QByteArray ApplicationController::encodePng(const QPixmap& pixmap) const {
    if (imageEncoderForTest_) {
        return imageEncoderForTest_(pixmap);
    }

    if (pixmap.isNull()) {
        return {};
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return bytes;
}

bool ApplicationController::applyLaunchAtLoginPreference() const {
    if (startupRegistry_ == nullptr) {
        return false;
    }

    return startupRegistry_->setLaunchAtLogin(
        config_.launchAtLogin,
        QCoreApplication::applicationFilePath());
}

QString ApplicationController::defaultFirstPrompt() const {
    const QString prompt = config_.firstPrompt.trimmed();
    if (!prompt.isEmpty()) {
        return prompt;
    }

    return config::defaultFirstPromptText();
}

QString ApplicationController::defaultTextQueryPrompt() const {
    const QString prompt = config_.textQueryPrompt.trimmed();
    if (!prompt.isEmpty()) {
        return prompt;
    }

    return config::defaultTextQueryPromptText();
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
    if (chatPanel_ != nullptr) {
        config_.chatPanelSize = chatPanel_->size();
    }
    if (settingsDialog_ != nullptr) {
        config_.settingsDialogSize = settingsDialog_->size();
        config_.settingsDialogPosition = settingsDialog_->pos();
    }
}

bool ApplicationController::saveConfigSnapshot() const {
    return configStore_ == nullptr || configStore_->save(config_);
}

void ApplicationController::syncBusyUi(const QString& statusOverride) {
    const bool busy = guard_.isBusy();
    const QString status = statusOverride.isEmpty() ? statusForState(guard_.state()) : statusOverride;
    lastStatusText_ = status;

    if (captureAction_ != nullptr) {
        captureAction_->setEnabled(!busy);
    }
    if (textQueryAction_ != nullptr) {
        textQueryAction_->setEnabled(!busy);
    }

    if (settingsDialog_ != nullptr) {
        settingsDialog_->setBusy(busy, status);
    }

    if (chatPanel_ != nullptr) {
        chatPanel_->setBusy(busy, status);
    }
}

}  // namespace ais::app
