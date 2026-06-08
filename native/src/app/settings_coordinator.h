#pragma once

#include <functional>

#include <QObject>
#include <QPointer>
#include <QString>

#include "app/app_busy_state.h"
#include "config/app_config.h"

class QWidget;

namespace ais::ai {
class ProviderTestRunner;
}  // namespace ais::ai

namespace ais::ui {
class SettingsDialog;
}  // namespace ais::ui

namespace ais::app {

class SettingsCoordinator final : public QObject {
    Q_OBJECT

public:
    struct Hooks {
        std::function<void(const QString&)> syncStatus;
        std::function<void(const config::AppConfig&)> onConfigApplied;
        std::function<void()> applyAppearance;
        std::function<void()> rememberWindowSizes;
        std::function<bool()> saveConfigSnapshot;
        std::function<bool()> refreshHotkeys;
        std::function<bool()> applyLaunchAtLogin;
        std::function<BusyState()> busyState;
        std::function<QString()> statusForCurrentState;
    };

    explicit SettingsCoordinator(Hooks hooks = {}, QObject* parent = nullptr);
    ~SettingsCoordinator() override;

    void setConfig(const config::AppConfig& config);
    [[nodiscard]] const config::AppConfig& config() const noexcept { return config_; }

    void setDialogParent(QWidget* parent);
    void setProviderTestRunner(ai::ProviderTestRunner* runner);

    void ensureSettingsDialog();
    void openSettings();
    void onSettingsDialogFinished(int result);

    void runProviderTest(bool imageMode);
    void fetchProviderModels();
    void applySettingsFromDialog();
    void completeProviderTestForTest(bool imageMode, bool success, const QString& textOrError);
    void closeSettingsDialogForTest();

    void applyDialogAppearance(const QString& theme);
    void setDialogBusy(bool busy, const QString& status);
    void rememberDialogSize(config::AppConfig* config) const;

private:
    [[nodiscard]] BusyState currentBusyState() const;
    [[nodiscard]] QString currentStatus() const;
    void syncStatus(const QString& status) const;
    [[nodiscard]] bool invokeOrTrue(const std::function<bool()>& callback) const;
    void handleProviderTestSuccess(bool imageMode, const QString& text);
    void handleProviderTestFailure(const QString& error);

    Hooks hooks_;
    config::AppConfig config_;
    QPointer<QWidget> dialogParent_;
    QPointer<ui::SettingsDialog> settingsDialog_;
    ai::ProviderTestRunner* providerTestRunner_ = nullptr;
};

}  // namespace ais::app
