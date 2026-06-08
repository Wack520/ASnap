#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;
class QString;
class QWidget;

namespace ais::config {
struct AppConfig;
}  // namespace ais::config

namespace ais::platform::windows {
class GlobalHotkeyHost;
class StartupRegistry;
}  // namespace ais::platform::windows

namespace ais::app {

class ShellIntegration final : public QObject {
    Q_OBJECT

public:
    struct Hooks {
        std::function<void()> onAiCaptureRequested;
        std::function<void()> onPlainCaptureRequested;
        std::function<void()> onSettingsRequested;
        std::function<void()> onQuitRequested;
    };

    struct Adapters {
        std::function<bool(const QString&)> registerAiHotkey;
        std::function<bool(const QString&)> registerPlainHotkey;
        std::function<void()> unregisterAiHotkey;
        std::function<void()> unregisterPlainHotkey;
        std::function<bool(bool, const QString&)> setLaunchAtLogin;
    };

    explicit ShellIntegration(Hooks hooks = {}, QObject* parent = nullptr);
    ~ShellIntegration() override;

    [[nodiscard]] bool initialize(QWidget* menuParent = nullptr);
    [[nodiscard]] bool refreshHotkeys(const config::AppConfig& config);
    [[nodiscard]] bool applyLaunchAtLoginPreference(const config::AppConfig& config);
    void setAiCaptureEnabled(bool enabled);
    void showMessage(const QString& message,
                     QSystemTrayIcon::MessageIcon icon,
                     int millisecondsTimeout);
    void releaseResources();

    void setAdaptersForTest(Adapters adapters);
    void requestAiCaptureForTest();
    void requestPlainCaptureForTest();
    void requestSettingsForTest();
    void requestQuitForTest();

private:
    void createTray(QWidget* menuParent);
    void ensureHotkeyHosts();
    void requestAiCapture();
    void requestPlainCapture();
    void requestSettings();
    void requestQuit();

    Hooks hooks_;
    Adapters adapters_;

    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QAction* captureAction_ = nullptr;
    QAction* screenshotAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* quitAction_ = nullptr;
    platform::windows::GlobalHotkeyHost* aiHotkeyHost_ = nullptr;
    platform::windows::GlobalHotkeyHost* screenshotHotkeyHost_ = nullptr;
    std::unique_ptr<platform::windows::StartupRegistry> startupRegistry_;
    bool resourcesReleased_ = false;
};

}  // namespace ais::app
