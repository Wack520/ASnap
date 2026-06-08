#include "app/shell_integration.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QSystemTrayIcon>

#include "config/app_config.h"
#include "platform/windows/global_hotkey_host.h"
#include "platform/windows/startup_registry.h"
#include "ui/icon_factory.h"

namespace ais::app {

ShellIntegration::ShellIntegration(Hooks hooks, QObject* parent)
    : QObject(parent),
      hooks_(std::move(hooks)) {}

ShellIntegration::~ShellIntegration() {
    releaseResources();
}

bool ShellIntegration::initialize(QWidget* menuParent) {
    createTray(menuParent);
    ensureHotkeyHosts();
    return trayIcon_ != nullptr;
}

bool ShellIntegration::refreshHotkeys(const config::AppConfig& config) {
    resourcesReleased_ = false;

    bool aiRegistered = false;
    if (adapters_.registerAiHotkey) {
        aiRegistered = adapters_.registerAiHotkey(config.aiShortcut);
    } else {
        ensureHotkeyHosts();
        aiRegistered = aiHotkeyHost_ != nullptr &&
                       aiHotkeyHost_->registerHotkey(config.aiShortcut);
    }

    bool screenshotRegistered = false;
    if (adapters_.registerPlainHotkey) {
        screenshotRegistered = adapters_.registerPlainHotkey(config.screenshotShortcut);
    } else {
        ensureHotkeyHosts();
        screenshotRegistered = screenshotHotkeyHost_ != nullptr &&
                               screenshotHotkeyHost_->registerHotkey(config.screenshotShortcut);
    }

    return aiRegistered && screenshotRegistered;
}

bool ShellIntegration::applyLaunchAtLoginPreference(const config::AppConfig& config) {
    const QString executablePath = QCoreApplication::applicationFilePath();
    if (adapters_.setLaunchAtLogin) {
        return adapters_.setLaunchAtLogin(config.launchAtLogin, executablePath);
    }

    if (startupRegistry_ == nullptr) {
        startupRegistry_ = std::make_unique<platform::windows::WindowsStartupRegistry>();
    }

    return startupRegistry_ != nullptr &&
           startupRegistry_->setLaunchAtLogin(config.launchAtLogin, executablePath);
}

void ShellIntegration::setAiCaptureEnabled(const bool enabled) {
    if (captureAction_ != nullptr) {
        captureAction_->setEnabled(enabled);
    }
}

void ShellIntegration::showMessage(const QString& message,
                                   const QSystemTrayIcon::MessageIcon icon,
                                   const int millisecondsTimeout) {
    if (trayIcon_ != nullptr) {
        trayIcon_->showMessage(ui::brandDisplayName(), message, icon, millisecondsTimeout);
    }
}

void ShellIntegration::releaseResources() {
    if (resourcesReleased_) {
        return;
    }
    resourcesReleased_ = true;

    if (adapters_.unregisterAiHotkey) {
        adapters_.unregisterAiHotkey();
    } else if (aiHotkeyHost_ != nullptr) {
        aiHotkeyHost_->unregisterHotkey();
    }

    if (adapters_.unregisterPlainHotkey) {
        adapters_.unregisterPlainHotkey();
    } else if (screenshotHotkeyHost_ != nullptr) {
        screenshotHotkeyHost_->unregisterHotkey();
    }

    if (trayIcon_ != nullptr) {
        trayIcon_->hide();
    }

    if (trayMenu_ != nullptr && trayMenu_->parentWidget() == nullptr) {
        trayMenu_->deleteLater();
        trayMenu_ = nullptr;
        captureAction_ = nullptr;
        screenshotAction_ = nullptr;
        settingsAction_ = nullptr;
        quitAction_ = nullptr;
    }
}

void ShellIntegration::setAdaptersForTest(Adapters adapters) {
    adapters_ = std::move(adapters);
}

void ShellIntegration::requestAiCaptureForTest() {
    requestAiCapture();
}

void ShellIntegration::requestPlainCaptureForTest() {
    requestPlainCapture();
}

void ShellIntegration::requestSettingsForTest() {
    requestSettings();
}

void ShellIntegration::requestQuitForTest() {
    requestQuit();
}

void ShellIntegration::createTray(QWidget* menuParent) {
    if (trayIcon_ != nullptr) {
        return;
    }

    trayIcon_ = new QSystemTrayIcon(this);
    trayMenu_ = menuParent != nullptr ? new QMenu(menuParent) : new QMenu();
    captureAction_ = trayMenu_->addAction(QStringLiteral("AI 截图"));
    screenshotAction_ = trayMenu_->addAction(QStringLiteral("普通截图"));
    settingsAction_ = trayMenu_->addAction(QStringLiteral("设置"));
    trayMenu_->addSeparator();
    quitAction_ = trayMenu_->addAction(QStringLiteral("退出"));

    connect(captureAction_, &QAction::triggered, this, &ShellIntegration::requestAiCapture);
    connect(screenshotAction_, &QAction::triggered, this, &ShellIntegration::requestPlainCapture);
    connect(settingsAction_, &QAction::triggered, this, &ShellIntegration::requestSettings);
    connect(quitAction_, &QAction::triggered, this, &ShellIntegration::requestQuit);

    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->setIcon(ui::createAppIcon());
    trayIcon_->setToolTip(ui::brandDisplayName());
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](const QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    requestAiCapture();
                }
            });
    trayIcon_->show();
}

void ShellIntegration::ensureHotkeyHosts() {
    if (adapters_.registerAiHotkey && adapters_.registerPlainHotkey &&
        adapters_.unregisterAiHotkey && adapters_.unregisterPlainHotkey) {
        return;
    }

    if (aiHotkeyHost_ == nullptr) {
        aiHotkeyHost_ = new platform::windows::GlobalHotkeyHost(1, this);
        connect(aiHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
                this, &ShellIntegration::requestAiCapture);
    }
    if (screenshotHotkeyHost_ == nullptr) {
        screenshotHotkeyHost_ = new platform::windows::GlobalHotkeyHost(2, this);
        connect(screenshotHotkeyHost_, &platform::windows::GlobalHotkeyHost::triggered,
                this, &ShellIntegration::requestPlainCapture);
    }
}

void ShellIntegration::requestAiCapture() {
    if (hooks_.onAiCaptureRequested) {
        hooks_.onAiCaptureRequested();
    }
}

void ShellIntegration::requestPlainCapture() {
    if (hooks_.onPlainCaptureRequested) {
        hooks_.onPlainCaptureRequested();
    }
}

void ShellIntegration::requestSettings() {
    if (hooks_.onSettingsRequested) {
        hooks_.onSettingsRequested();
    }
}

void ShellIntegration::requestQuit() {
    if (hooks_.onQuitRequested) {
        hooks_.onQuitRequested();
    }
}

}  // namespace ais::app
