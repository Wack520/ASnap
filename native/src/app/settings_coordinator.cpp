#include "app/settings_coordinator.h"

#include <utility>

#include <QCoreApplication>
#include <QDialog>
#include <QTimer>

#include "ai/provider_test_runner.h"
#include "config/provider_preset.h"
#include "ui/settings/settingsdialog.h"

namespace ais::app {

namespace {

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

}  // namespace

SettingsCoordinator::SettingsCoordinator(Hooks hooks, QObject* parent)
    : QObject(parent),
      hooks_(std::move(hooks)) {}

SettingsCoordinator::~SettingsCoordinator() {
    if (settingsDialog_ != nullptr) {
        settingsDialog_->close();
        settingsDialog_->deleteLater();
    }
}

void SettingsCoordinator::setConfig(const config::AppConfig& config) {
    config_ = config;
}

void SettingsCoordinator::setDialogParent(QWidget* parent) {
    dialogParent_ = parent;
}

void SettingsCoordinator::setProviderTestRunner(ai::ProviderTestRunner* runner) {
    providerTestRunner_ = runner;
}

void SettingsCoordinator::ensureSettingsDialog() {
    if (settingsDialog_ != nullptr) {
        return;
    }

    settingsDialog_ = new ui::SettingsDialog(config_, dialogParent_);
    settingsDialog_->setModal(false);
    connect(settingsDialog_, &ui::SettingsDialog::saveRequested,
            this, &SettingsCoordinator::applySettingsFromDialog);
    connect(settingsDialog_, &ui::SettingsDialog::fetchModelsRequested,
            this, &SettingsCoordinator::fetchProviderModels);
    connect(settingsDialog_, &ui::SettingsDialog::testConnectionRequested,
            this, [this]() { runProviderTest(false); });
    connect(settingsDialog_, &ui::SettingsDialog::testImageUnderstandingRequested,
            this, [this]() { runProviderTest(true); });
    connect(settingsDialog_, &QDialog::finished,
            this, &SettingsCoordinator::onSettingsDialogFinished);
}

void SettingsCoordinator::openSettings() {
    ensureSettingsDialog();
    if (settingsDialog_ == nullptr) {
        return;
    }

    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
    syncStatus(currentStatus());
}

void SettingsCoordinator::onSettingsDialogFinished(const int result) {
    if (settingsDialog_ == nullptr) {
        return;
    }

    if (result == QDialog::Accepted) {
        applySettingsFromDialog();
        return;
    }

    if (hooks_.rememberWindowSizes != nullptr) {
        hooks_.rememberWindowSizes();
    }
    (void)invokeOrTrue(hooks_.saveConfigSnapshot);
    syncStatus(currentStatus());
}

void SettingsCoordinator::runProviderTest(const bool imageMode) {
    if (settingsDialog_ == nullptr || providerTestRunner_ == nullptr) {
        return;
    }
    if (currentBusyState() == BusyState::TestingProvider) {
        return;
    }

    const QString runningStatus = imageMode
        ? QStringLiteral("正在测试图片理解…")
        : QStringLiteral("正在测试文字连接…");

    settingsDialog_->setActionMode(
        imageMode ? ui::SettingsDialog::ActionMode::TestImage
                  : ui::SettingsDialog::ActionMode::TestText,
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

    syncStatus(started ? runningStatus : currentStatus());
}

void SettingsCoordinator::fetchProviderModels() {
    if (settingsDialog_ == nullptr || providerTestRunner_ == nullptr) {
        return;
    }
    if (currentBusyState() == BusyState::TestingProvider) {
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
                syncStatus(status);
            });
        },
        [this](QString error) {
            QTimer::singleShot(0, this, [this, error = std::move(error)]() {
                handleProviderTestFailure(error);
            });
        });

    syncStatus(started ? runningStatus : currentStatus());
}

void SettingsCoordinator::applySettingsFromDialog() {
    if (settingsDialog_ == nullptr) {
        return;
    }

    config_ = settingsDialog_->currentConfig();
    config_.settingsDialogSize = settingsDialog_->size();

    if (hooks_.onConfigApplied != nullptr) {
        hooks_.onConfigApplied(config_);
    }
    if (hooks_.rememberWindowSizes != nullptr) {
        hooks_.rememberWindowSizes();
    }

    const bool saved = invokeOrTrue(hooks_.saveConfigSnapshot);
    const bool hotkeyRegistered = invokeOrTrue(hooks_.refreshHotkeys);
    const bool launchPreferenceApplied = invokeOrTrue(hooks_.applyLaunchAtLogin);

    if (hooks_.applyAppearance != nullptr) {
        hooks_.applyAppearance();
    }

    if (!saved) {
        syncStatus(QStringLiteral("Settings could not be saved"));
    } else if (!launchPreferenceApplied && !hotkeyRegistered) {
        syncStatus(QStringLiteral("Settings saved, but startup and hotkey registration failed"));
    } else if (!launchPreferenceApplied) {
        syncStatus(QStringLiteral("Settings saved, but startup registration failed"));
    } else if (!hotkeyRegistered) {
        syncStatus(QStringLiteral("Settings saved, but hotkey registration failed"));
    } else {
        syncStatus(QStringLiteral("Settings saved"));
    }
}

void SettingsCoordinator::completeProviderTestForTest(const bool imageMode,
                                                      const bool success,
                                                      const QString& textOrError) {
    if (success) {
        handleProviderTestSuccess(imageMode, textOrError);
        return;
    }

    handleProviderTestFailure(textOrError);
}

void SettingsCoordinator::closeSettingsDialogForTest() {
    if (settingsDialog_ == nullptr) {
        return;
    }

    settingsDialog_->close();
    settingsDialog_->deleteLater();
    settingsDialog_ = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void SettingsCoordinator::applyDialogAppearance(const QString& theme) {
    if (settingsDialog_ != nullptr) {
        settingsDialog_->applyAppearance(theme);
    }
}

void SettingsCoordinator::setDialogBusy(const bool busy, const QString& status) {
    if (settingsDialog_ != nullptr) {
        settingsDialog_->setBusy(busy, status);
    }
}

void SettingsCoordinator::rememberDialogSize(config::AppConfig* config) const {
    if (config != nullptr && settingsDialog_ != nullptr) {
        config->settingsDialogSize = settingsDialog_->size();
    }
}

BusyState SettingsCoordinator::currentBusyState() const {
    return hooks_.busyState != nullptr ? hooks_.busyState() : BusyState::Idle;
}

QString SettingsCoordinator::currentStatus() const {
    if (hooks_.statusForCurrentState != nullptr) {
        return hooks_.statusForCurrentState();
    }

    switch (currentBusyState()) {
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

void SettingsCoordinator::syncStatus(const QString& status) const {
    if (hooks_.syncStatus != nullptr) {
        hooks_.syncStatus(status);
    }
}

bool SettingsCoordinator::invokeOrTrue(const std::function<bool()>& callback) const {
    return callback == nullptr || callback();
}

void SettingsCoordinator::handleProviderTestSuccess(const bool imageMode, const QString& text) {
    const QString prefix = imageMode
        ? QStringLiteral("图片理解测试通过")
        : QStringLiteral("文字连接测试通过");
    const QString status = text.trimmed().isEmpty()
        ? prefix
        : QStringLiteral("%1: %2").arg(prefix, text.trimmed());

    syncStatus(status);
}

void SettingsCoordinator::handleProviderTestFailure(const QString& error) {
    syncStatus(QStringLiteral("测试失败：%1").arg(error));
}

}  // namespace ais::app
