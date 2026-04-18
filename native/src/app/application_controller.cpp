#include "app/application_controller.h"

#include <memory>

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
#include "chat/chat_session.h"
#include "config/config_store.h"
#include "config/provider_preset.h"
#include "platform/windows/global_hotkey_host.h"
#include "platform/windows/startup_registry.h"
#include "ui/icon_factory.h"
#include "ui/chat/floating_chat_panel.h"
#include "ui/panel_placement.h"
#include "ui/settings/settingsdialog.h"

namespace ais::app {

namespace {

constexpr int kMaxEmptyResponseRetries = 3;

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

[[nodiscard]] bool messagesContainImage(const QList<chat::ChatMessage>& messages) {
    for (const chat::ChatMessage& message : messages) {
        if (message.hasImage()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int defaultEmptyRetryDelayMs(const bool hasImageContext,
                                           const int emptyRetryAttempt) {
    if (!hasImageContext) {
        return 80;
    }

    switch (qMax(0, emptyRetryAttempt)) {
    case 0:
        return 1200;
    case 1:
        return 2500;
    default:
        return 5000;
    }
}

[[nodiscard]] bool isAssetUploadFailure(const QString& error) {
    return error.contains(QStringLiteral("Asset upload returned 400"), Qt::CaseInsensitive) ||
           error.contains(QStringLiteral("asset upload"), Qt::CaseInsensitive);
}

}  // namespace

ApplicationController::ApplicationController(QObject* parent)
    : QObject(parent) {}

ApplicationController::~ApplicationController() {
    rememberWindowSizes();
    (void)saveConfigSnapshot();
    clearOverlay();

    if (aiHotkeyHost_ != nullptr) {
        aiHotkeyHost_->unregisterHotkey();
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
    (void)applyLaunchAtLoginPreference();
    ensureChatPanel();
    createTray();

    aiHotkeyHost_ = new platform::windows::GlobalHotkeyHost(1, this);
    screenshotHotkeyHost_ = new platform::windows::GlobalHotkeyHost(2, this);
    connect(aiHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
            this, &ApplicationController::startCapture);
    connect(screenshotHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
            this, &ApplicationController::startPlainCapture);

    if (!registerHotkeys() && trayIcon_ != nullptr) {
        trayIcon_->showMessage(
            ui::brandDisplayName(),
            QStringLiteral("全局快捷键注册失败，请检查 AI 快捷键 / 截图快捷键是否冲突"),
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
    requestStreamStarter_ = std::move(starter);
}

void ApplicationController::setEmptyRetryDelayOverrideForTest(const int delayMs) {
    emptyRetryDelayOverrideMs_ = delayMs;
}

int ApplicationController::emptyRetryDelayMsForTest(const bool hasImageContext,
                                                    const int emptyRetryAttempt) const {
    return emptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
}

void ApplicationController::seedConversationForTest(const QString& initialUserText) {
    currentSession_ = std::make_shared<chat::ChatSession>();
    currentSession_->beginWithCapture(QByteArray("png-image"));
    currentSession_->addUserText(initialUserText);
    queuedFollowUpTexts_.clear();

    ensureChatPanel();
    if (chatPanel_ != nullptr) {
        chatPanel_->bindSession(currentSession_);
        chatPanel_->setBusy(false, QStringLiteral("Ready"));
    }
    syncBusyUi(QStringLiteral("Ready"));
}

void ApplicationController::followUpRequestedForTest(const QString& text) {
    onFollowUpRequested(text);
}

QString ApplicationController::queuedFollowUpTextForTest(const int index) const {
    if (index < 0 || index >= queuedFollowUpTexts_.size()) {
        return {};
    }
    return queuedFollowUpTexts_.at(index);
}

int ApplicationController::messageCountForTest() const {
    return currentSession_ == nullptr ? 0 : currentSession_->messages().size();
}

QString ApplicationController::lastUserMessageTextForTest() const {
    if (currentSession_ == nullptr) {
        return {};
    }

    const auto& messages = currentSession_->messages();
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        if (it->role == chat::ChatRole::User && !it->text.isEmpty()) {
            return it->text;
        }
    }
    return {};
}

QString ApplicationController::lastAssistantMessageTextForTest() const {
    if (currentSession_ == nullptr || currentSession_->messages().isEmpty()) {
        return {};
    }

    const auto& messages = currentSession_->messages();
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        if (it->role == chat::ChatRole::Assistant) {
            return it->text;
        }
    }
    return {};
}

QString ApplicationController::lastAssistantReasoningForTest() const {
    return currentSession_ == nullptr ? QString() : currentSession_->latestAssistantReasoning();
}

void ApplicationController::confirmCaptureForTest(const capture::CaptureSelection& selection, const bool sendToAi) {
    activeCaptureMode_ = sendToAi ? CaptureLaunchMode::AiAssistant : CaptureLaunchMode::PlainScreenshot;
    handleConfirmedCapture(selection);
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
    overlay_ = new capture::CaptureOverlay(snapshot);
    connect(overlay_, &capture::CaptureOverlay::captureConfirmed,
            this, &ApplicationController::onCaptureConfirmed);
    connect(overlay_, &capture::CaptureOverlay::captureCancelled,
            this, &ApplicationController::onCaptureCancelled);
    connect(overlay_, &QObject::destroyed, this, [this]() { overlay_ = nullptr; });

    overlay_->show();
    overlay_->activateWindow();
    overlay_->raise();
}

void ApplicationController::openSettings() {
    ensureSettingsDialog();

    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
    syncBusyUi();
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

    if (chatPanel_ != nullptr && currentSession_) {
        chatPanel_->show();
        chatPanel_->raise();
        chatPanel_->activateWindow();
    }
}

void ApplicationController::onFollowUpRequested(const QString& text) {
    if (currentSession_ == nullptr) {
        return;
    }

    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        return;
    }

    if (guard_.state() == BusyState::RequestInFlight) {
        queueFollowUp(trimmedText);
        return;
    }
    if (guard_.isBusy()) {
        return;
    }

    currentSession_->addUserText(trimmedText);
    if (chatPanel_ != nullptr) {
        chatPanel_->bindSession(currentSession_);
    }

    if (!sendCurrentSessionRequest(QStringLiteral("Sending follow-up..."))) {
        if (chatPanel_ != nullptr) {
            chatPanel_->bindSession(currentSession_);
        }
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
    captureAction_ = trayMenu_->addAction(QStringLiteral("AI 截图"));
    screenshotAction_ = trayMenu_->addAction(QStringLiteral("普通截图"));
    settingsAction_ = trayMenu_->addAction(QStringLiteral("设置"));
    trayMenu_->addSeparator();
    quitAction_ = trayMenu_->addAction(QStringLiteral("退出"));

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
    if (aiHotkeyHost_ == nullptr || screenshotHotkeyHost_ == nullptr) {
        return false;
    }

    const bool aiRegistered = aiHotkeyHost_->registerHotkey(config_.aiShortcut);
    const bool screenshotRegistered = screenshotHotkeyHost_->registerHotkey(config_.screenshotShortcut);
    return aiRegistered && screenshotRegistered;
}

void ApplicationController::clearOverlay() {
    if (overlay_ == nullptr) {
        return;
    }

    overlay_->hide();
    overlay_->deleteLater();
    overlay_ = nullptr;
}

void ApplicationController::cancelCurrentConversation(const bool clearSession) {
    if (aiClient_ != nullptr) {
        aiClient_->cancelActiveRequest();
    } else {
        guard_.leave(BusyState::RequestInFlight);
    }

    if (clearSession) {
        currentSession_.reset();
    }
    queuedFollowUpTexts_.clear();

    if (chatPanel_ != nullptr) {
        chatPanel_->bindSession(currentSession_);
        chatPanel_->setBusy(false, statusForState(guard_.state()));
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

    if (chatPanel_ != nullptr && currentSession_) {
        chatPanel_->show();
        chatPanel_->raise();
        chatPanel_->activateWindow();
    }
}

void ApplicationController::beginSessionFromSelection(const capture::CaptureSelection& selection) {
    queuedFollowUpTexts_.clear();
    currentSession_ = std::make_shared<chat::ChatSession>();
    currentSession_->beginWithCapture(encodePng(selection.image));
    currentSession_->addUserText(defaultFirstPrompt());

    ensureChatPanel();
    if (chatPanel_ == nullptr) {
        syncBusyUi(QStringLiteral("Chat panel is unavailable"));
        return;
    }

    chatPanel_->bindSession(currentSession_);
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

    if (!sendCurrentSessionRequest(QStringLiteral("Analyzing screenshot..."))) {
        chatPanel_->bindSession(currentSession_);
        syncBusyUi(QStringLiteral("Unable to start AI request"));
    }
}

bool ApplicationController::sendCurrentSessionRequest(const QString& busyStatus,
                                                      const int emptyRetryAttempt,
                                                      const config::ProviderProfile* requestProfileOverride,
                                                      const bool allowAssetUploadFallback) {
    if (currentSession_ == nullptr) {
        return false;
    }
    const config::ProviderProfile requestProfile =
        withDefaults(requestProfileOverride != nullptr ? *requestProfileOverride
                                                       : config_.activeProfile);

    const RequestStreamStarter requestStarter = requestStreamStarter_
        ? requestStreamStarter_
        : [this](const config::ProviderProfile& profile,
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

    if (chatPanel_ != nullptr) {
        chatPanel_->setBusy(true, busyStatus);
    }

    currentSession_->beginAssistantResponse();
    if (chatPanel_ != nullptr) {
        chatPanel_->bindSession(currentSession_);
    }

    const bool started = requestStarter(
        requestProfile,
        currentSession_->messages(),
        [this](QString textDelta) {
            if (currentSession_ != nullptr) {
                currentSession_->appendAssistantTextDelta(textDelta);
            }

            if (chatPanel_ != nullptr && currentSession_ != nullptr) {
                chatPanel_->scheduleSessionRefresh();
            }
        },
        [this](QString reasoningDelta) {
            if (currentSession_ != nullptr) {
                currentSession_->appendAssistantReasoningDelta(reasoningDelta);
            }

            if (chatPanel_ != nullptr && currentSession_ != nullptr) {
                chatPanel_->scheduleSessionRefresh();
            }
        },
        [this, emptyRetryAttempt]() {
            QTimer::singleShot(0, this, [this, emptyRetryAttempt]() {
                handleRequestCompleted(emptyRetryAttempt);
            });
        },
        [this, emptyRetryAttempt, requestProfile, allowAssetUploadFallback](QString error) {
            QTimer::singleShot(0, this, [this,
                                         emptyRetryAttempt,
                                         requestProfile,
                                         allowAssetUploadFallback,
                                         error = std::move(error)]() {
                if (currentSession_ != nullptr &&
                    allowAssetUploadFallback &&
                    requestProfile.protocol == config::ProviderProtocol::OpenAiResponses &&
                    messagesContainImage(currentSession_->messages()) &&
                    isAssetUploadFailure(error)) {
                    config::ProviderProfile fallbackProfile = requestProfile;
                    fallbackProfile.protocol = config::ProviderProtocol::OpenAiCompatible;
                    currentSession_->removeLastAssistantMessage();
                    if (chatPanel_ != nullptr) {
                        chatPanel_->bindSession(currentSession_);
                    }
                    syncBusyUi(QStringLiteral("图片上传失败，正在切换兼容链路重试…"));
                    if (sendCurrentSessionRequest(QStringLiteral("图片上传失败，正在切换兼容链路重试…"),
                                                  emptyRetryAttempt,
                                                  &fallbackProfile,
                                                  false)) {
                        return;
                    }
                }

                const QString friendlyError = QStringLiteral("Request failed: %1").arg(error);
                if (currentSession_ != nullptr) {
                    currentSession_->failAssistantResponse(friendlyError);
                }

                if (chatPanel_ != nullptr && currentSession_ != nullptr) {
                    chatPanel_->bindSession(currentSession_);
                }
                syncBusyUi(friendlyError);
            });
        },
        emptyRetryAttempt);

    if (!started) {
        if (currentSession_ != nullptr) {
            currentSession_->failAssistantResponse(QStringLiteral("Unable to send now. The app is busy."));
        }
        if (chatPanel_ != nullptr) {
            chatPanel_->bindSession(currentSession_);
            chatPanel_->setBusy(false, statusForState(guard_.state()));
        }
        return false;
    }

    syncBusyUi(busyStatus);
    return true;
}

void ApplicationController::queueFollowUp(const QString& text) {
    if (text.isEmpty()) {
        return;
    }

    queuedFollowUpTexts_.append(text);
    syncBusyUi(QStringLiteral("已排队 %1 条，当前回复结束后自动发送…").arg(queuedFollowUpTexts_.size()));
}

void ApplicationController::scheduleQueuedFollowUpSend() {
    if (queuedFollowUpTexts_.isEmpty() || currentSession_ == nullptr || guard_.isBusy()) {
        return;
    }

    const QString nextText = queuedFollowUpTexts_.takeFirst();
    currentSession_->addUserText(nextText);
    if (chatPanel_ != nullptr) {
        chatPanel_->bindSession(currentSession_);
    }

    if (!sendCurrentSessionRequest(QStringLiteral("正在发送排队追问…"))) {
        if (chatPanel_ != nullptr) {
            chatPanel_->bindSession(currentSession_);
        }
        syncBusyUi(QStringLiteral("Unable to start AI request"));
    }
}

void ApplicationController::handleRequestCompleted(const int emptyRetryAttempt) {
    bool shouldScheduleQueuedFollowUp = false;

    if (currentSession_ != nullptr) {
        currentSession_->finalizeAssistantResponse();
        const auto& messages = currentSession_->messages();
        const bool hasImageContext = messagesContainImage(messages);
        const bool hasEmptyAssistantReply =
            !messages.isEmpty() &&
            messages.constLast().role == chat::ChatRole::Assistant &&
            messages.constLast().text.trimmed().isEmpty() &&
            messages.constLast().reasoningText.trimmed().isEmpty();

        if (hasEmptyAssistantReply && emptyRetryAttempt < kMaxEmptyResponseRetries) {
            const int retryDelayMs = emptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
            if (aiClient_ != nullptr) {
                aiClient_->cancelActiveRequest();
            }
            currentSession_->removeLastAssistantMessage();
            if (chatPanel_ != nullptr) {
                chatPanel_->bindSession(currentSession_);
            }
            syncBusyUi(QStringLiteral("AI 返回空内容，正在自动重试…"));
            QTimer::singleShot(retryDelayMs, this, [this, nextAttempt = emptyRetryAttempt + 1]() {
                if (!sendCurrentSessionRequest(QStringLiteral("AI 返回空内容，正在自动重试…"),
                                               nextAttempt)) {
                    if (currentSession_ != nullptr) {
                        currentSession_->failAssistantResponse(QStringLiteral("(empty response)"));
                    }
                    if (chatPanel_ != nullptr) {
                        chatPanel_->bindSession(currentSession_);
                    }
                    syncBusyUi(QStringLiteral("Ready"));
                }
            });
            return;
        }

        if (hasEmptyAssistantReply) {
            currentSession_->failAssistantResponse(QStringLiteral("(empty response)"));
        }

        if (chatPanel_ != nullptr) {
            chatPanel_->bindSession(currentSession_);
        }
        shouldScheduleQueuedFollowUp = !queuedFollowUpTexts_.isEmpty();
    }

    syncBusyUi(QStringLiteral("Ready"));
    if (shouldScheduleQueuedFollowUp) {
        scheduleQueuedFollowUpSend();
    }
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
    syncBusyUi(QStringLiteral("测试失败：%1").arg(error));
}

void ApplicationController::applySettingsFromDialog() {
    if (settingsDialog_ == nullptr) {
        return;
    }

    config_ = settingsDialog_->currentConfig();
    applyConfigDefaults();
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

int ApplicationController::emptyRetryDelayMs(const bool hasImageContext,
                                             const int emptyRetryAttempt) const {
    if (emptyRetryDelayOverrideMs_ >= 0) {
        return emptyRetryDelayOverrideMs_;
    }

    return defaultEmptyRetryDelayMs(hasImageContext, emptyRetryAttempt);
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

    if (settingsDialog_ != nullptr) {
        settingsDialog_->setBusy(busy, status);
    }

    if (chatPanel_ != nullptr) {
        chatPanel_->setBusy(busy, status);
    }
}

}  // namespace ais::app
