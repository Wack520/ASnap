#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QStringList>

#include "ai/ai_client.h"
#include "app/request_guard.h"
#include "config/app_config.h"

class QAction;
class QMenu;
class QPixmap;
class QSystemTrayIcon;

namespace ais::ai {
class ProviderTestRunner;
}  // namespace ais::ai

namespace ais::capture {
class CaptureOverlay;
struct CaptureSelection;
class DesktopCaptureService;
}  // namespace ais::capture

namespace ais::chat {
class ChatSession;
}  // namespace ais::chat

namespace ais::config {
class ConfigStore;
}  // namespace ais::config

namespace ais::platform::windows {
class GlobalHotkeyHost;
class StartupRegistry;
}  // namespace ais::platform::windows

namespace ais::ui {
class FloatingChatPanel;
class SettingsDialog;
}  // namespace ais::ui

namespace ais::app {

class ApplicationController final : public QObject {
    Q_OBJECT

public:
    enum class CaptureLaunchMode {
        AiAssistant,
        PlainScreenshot,
    };

    using RequestStreamStarter = std::function<bool(const config::ProviderProfile&,
                                                    const QList<chat::ChatMessage>&,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::CompletionHandler,
                                                    ai::AiClient::FailureHandler,
                                                    int retryAttempt)>;

    explicit ApplicationController(QObject* parent = nullptr);
    ~ApplicationController() override;

    [[nodiscard]] bool initialize();

    void forceBusyStateForTest(BusyState state);
    [[nodiscard]] bool canStartCaptureForTest() const;
    void ensureSettingsDialogForTest();
    void ensureChatPanelForTest();
    void closeSettingsDialogForTest();
    void closeChatPanelForTest();
    void completeProviderTestForTest(bool imageMode, bool success, const QString& textOrError);
    [[nodiscard]] QString lastStatusTextForTest() const { return lastStatusText_; }
    void setRequestStreamStarterForTest(RequestStreamStarter starter);
    void setEmptyRetryDelayOverrideForTest(int delayMs);
    [[nodiscard]] int emptyRetryDelayMsForTest(bool hasImageContext, int emptyRetryAttempt) const;
    void seedConversationForTest(const QString& initialUserText);
    void followUpRequestedForTest(const QString& text);
    [[nodiscard]] int queuedFollowUpCountForTest() const noexcept { return queuedFollowUpTexts_.size(); }
    [[nodiscard]] QString queuedFollowUpTextForTest(int index) const;
    [[nodiscard]] int messageCountForTest() const;
    [[nodiscard]] QString lastUserMessageTextForTest() const;
    [[nodiscard]] QString lastAssistantMessageTextForTest() const;
    [[nodiscard]] QString lastAssistantReasoningForTest() const;
    void confirmCaptureForTest(const capture::CaptureSelection& selection, bool sendToAi);

private slots:
    void startCapture();
    void startPlainCapture();
    void openSettings();
    void onCaptureConfirmed(const ais::capture::CaptureSelection& selection);
    void onCaptureCancelled();
    void onFollowUpRequested(const QString& text);
    void onRecaptureRequested();
    void onSettingsSaveRequested();
    void onSettingsFetchModelsRequested();
    void onSettingsTextTestRequested();
    void onSettingsImageTestRequested();
    void onSettingsDialogFinished(int result);
    void onChatPanelDismissed();
    void quitRequested();

private:
    void ensureServiceOwnership();
    void ensureChatPanel();
    void ensureSettingsDialog();
    void createTray();
    void loadConfig();
    void applyConfigDefaults();
    void applyAppearance();
    [[nodiscard]] bool registerHotkeys();

    void clearOverlay();
    void cancelCurrentConversation(bool clearSession = true);
    void startCaptureWorkflow(CaptureLaunchMode mode);
    void handleConfirmedCapture(const ais::capture::CaptureSelection& selection);
    void handlePlainScreenshotCapture(const ais::capture::CaptureSelection& selection);
    void beginSessionFromSelection(const ais::capture::CaptureSelection& selection);
    [[nodiscard]] bool sendCurrentSessionRequest(
        const QString& busyStatus,
        int emptyRetryAttempt = 0,
        const config::ProviderProfile* requestProfileOverride = nullptr,
        bool allowAssetUploadFallback = true);
    void queueFollowUp(const QString& text);
    void scheduleQueuedFollowUpSend();
    void handleRequestCompleted(int emptyRetryAttempt);
    void fetchProviderModels();
    void runProviderTest(bool imageMode);
    void applySettingsFromDialog();
    void handleProviderTestSuccess(bool imageMode, const QString& text);
    void handleProviderTestFailure(const QString& error);

    [[nodiscard]] QByteArray encodePng(const QPixmap& pixmap) const;
    [[nodiscard]] bool applyLaunchAtLoginPreference() const;
    [[nodiscard]] QString defaultFirstPrompt() const;
    [[nodiscard]] int emptyRetryDelayMs(bool hasImageContext, int emptyRetryAttempt) const;
    [[nodiscard]] QString statusForState(BusyState state) const;
    void rememberWindowSizes();
    [[nodiscard]] bool saveConfigSnapshot() const;
    void syncBusyUi(const QString& statusOverride = {});

    RequestGuard guard_;
    config::AppConfig config_;

    std::unique_ptr<config::ConfigStore> configStore_;
    std::unique_ptr<capture::DesktopCaptureService> captureService_;
    std::unique_ptr<ai::AiClient> aiClient_;
    std::unique_ptr<ai::ProviderTestRunner> providerTestRunner_;
    std::shared_ptr<chat::ChatSession> currentSession_;

    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QAction* captureAction_ = nullptr;
    QAction* screenshotAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* quitAction_ = nullptr;
    platform::windows::GlobalHotkeyHost* aiHotkeyHost_ = nullptr;
    platform::windows::GlobalHotkeyHost* screenshotHotkeyHost_ = nullptr;
    std::unique_ptr<platform::windows::StartupRegistry> startupRegistry_;
    ui::SettingsDialog* settingsDialog_ = nullptr;
    ui::FloatingChatPanel* chatPanel_ = nullptr;
    capture::CaptureOverlay* overlay_ = nullptr;

    bool initialized_ = false;
    QString lastStatusText_ = QStringLiteral("Ready");
    QStringList queuedFollowUpTexts_;
    RequestStreamStarter requestStreamStarter_;
    CaptureLaunchMode activeCaptureMode_ = CaptureLaunchMode::AiAssistant;
    int emptyRetryDelayOverrideMs_ = -1;
};

}  // namespace ais::app
