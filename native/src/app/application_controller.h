#pragma once

#include <memory>

#include <QObject>
#include <QString>

#include "app/request_guard.h"
#include "config/app_config.h"

namespace ais::ai {
class AiClient;
class ProviderTestRunner;
}  // namespace ais::ai

namespace ais::capture {
struct CaptureSelection;
class DesktopCaptureService;
}  // namespace ais::capture

namespace ais::config {
class ConfigStore;
}  // namespace ais::config

namespace ais::ui {
class FloatingChatPanel;
}  // namespace ais::ui

namespace ais::app {

class CaptureCoordinator;
class ConversationRuntime;
class SettingsCoordinator;
class ShellIntegration;

class ApplicationController final : public QObject {
    Q_OBJECT

public:
    explicit ApplicationController(QObject* parent = nullptr);
    ~ApplicationController() override;

    [[nodiscard]] bool initialize();

    void forceBusyStateForTest(BusyState state) {
        guard_.leave(BusyState::Capturing);
        guard_.leave(BusyState::RequestInFlight);
        guard_.leave(BusyState::TestingProvider);
        if (state != BusyState::Idle) {
            (void)guard_.tryEnter(state);
        }
    }
    [[nodiscard]] bool canStartCaptureForTest() const;
    void ensureSettingsDialogForTest() {
        ensureServiceOwnership();
        ensureSettingsDialog();
    }
    void closeSettingsDialogForTest();
    void completeProviderTestForTest(bool imageMode, bool success, const QString& textOrError);
    [[nodiscard]] QString lastStatusTextForTest() const { return lastStatusText_; }
    void confirmCaptureForTest(const capture::CaptureSelection& selection, bool sendToAi);

private slots:
    void startCapture() { beginCapture(true); }
    void startPlainCapture() { beginCapture(false); }
    void openSettings();
    void onFollowUpRequested(const QString& text);
    void onChatPanelDismissed();
    void quitRequested();

private:
    void ensureServiceOwnership();
    void beginCapture(bool sendToAi);
    void ensureChatPanel();
    void ensureSettingsDialog();
    void loadConfig();
    void applyConfigDefaults();
    void applyAppearance();
    void applyCaptureMode();

    void handlePlainScreenshotCapture(const capture::CaptureSelection& selection);

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
    std::unique_ptr<ConversationRuntime> conversationRuntime_;
    std::unique_ptr<CaptureCoordinator> captureCoordinator_;
    std::unique_ptr<SettingsCoordinator> settingsCoordinator_;
    std::unique_ptr<ShellIntegration> shellIntegration_;
    ui::FloatingChatPanel* chatPanel_ = nullptr;

    bool initialized_ = false;
    QString lastStatusText_ = QStringLiteral("Ready");
};

}  // namespace ais::app
