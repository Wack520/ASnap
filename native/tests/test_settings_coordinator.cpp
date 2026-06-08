#include <QtTest/QtTest>

#include "ai/network_transport.h"
#include "ai/provider_test_runner.h"
#include "app/app_busy_state.h"
#include "app/request_guard.h"
#include "app/settings_coordinator.h"
#include "config/app_config.h"
#include "ui/settings/settingsdialog.h"

using ais::app::BusyState;
using ais::app::SettingsCoordinator;

namespace {

class CountingTransport final : public ais::ai::INetworkTransport {
public:
    int getCalls = 0;
    int postCalls = 0;
    int postStreamCalls = 0;

    void get(const ais::ai::RequestSpec&,
             SuccessHandler,
             FailureHandler) override {
        getCalls += 1;
    }

    void post(const ais::ai::RequestSpec&,
              SuccessHandler,
              FailureHandler) override {
        postCalls += 1;
    }

    void postStream(const ais::ai::RequestSpec&,
                    ChunkHandler,
                    CompletionHandler,
                    FailureHandler) override {
        postStreamCalls += 1;
    }

    void cancelActiveRequest() override {}
};

SettingsCoordinator::Hooks makeHooks(BusyState& state,
                                     QString& statusText) {
    SettingsCoordinator::Hooks hooks;
    hooks.syncStatus = [&](const QString& status) {
        statusText = status;
    };
    hooks.onConfigApplied = [](const ais::config::AppConfig&) {};
    hooks.applyAppearance = []() {};
    hooks.rememberWindowSizes = []() {};
    hooks.saveConfigSnapshot = []() {
        return true;
    };
    hooks.refreshHotkeys = []() {
        return true;
    };
    hooks.applyLaunchAtLogin = []() {
        return true;
    };
    hooks.busyState = [&]() {
        return state;
    };
    hooks.statusForCurrentState = [&]() {
        return state == BusyState::TestingProvider
            ? QStringLiteral("Running provider test...")
            : QStringLiteral("Ready");
    };
    return hooks;
}

}  // namespace

class SettingsCoordinatorTests final : public QObject {
    Q_OBJECT

private slots:
    void providerTestCompletionRefreshesStatusAfterSettingsDialogCloses();
    void busyGuardSkipsProviderTestAndModelFetch();
    void applySettingsFromDialogPropagatesConfigAndReportsSaveFailure();
    void applySettingsFromDialogPreservesCombinedFailureStatus();
};

void SettingsCoordinatorTests::providerTestCompletionRefreshesStatusAfterSettingsDialogCloses() {
    BusyState state = BusyState::Idle;
    QString statusText = QStringLiteral("Ready");
    QWidget chatParent;

    SettingsCoordinator coordinator(makeHooks(state, statusText));
    coordinator.setConfig(ais::config::AppConfig{});
    coordinator.setDialogParent(&chatParent);
    coordinator.ensureSettingsDialog();
    state = BusyState::TestingProvider;
    coordinator.closeSettingsDialogForTest();
    state = BusyState::Idle;
    coordinator.completeProviderTestForTest(false, true, QStringLiteral("OK"));

    QCOMPARE(statusText, QStringLiteral("文字连接测试通过: OK"));
}

void SettingsCoordinatorTests::busyGuardSkipsProviderTestAndModelFetch() {
    BusyState state = BusyState::TestingProvider;
    QString statusText = QStringLiteral("Ready");
    QWidget chatParent;
    ais::app::RequestGuard guard;
    auto transport = std::make_unique<CountingTransport>();
    CountingTransport* transportPtr = transport.get();
    ais::ai::ProviderTestRunner runner(std::move(transport), guard);

    SettingsCoordinator coordinator(makeHooks(state, statusText));
    coordinator.setConfig(ais::config::AppConfig{});
    coordinator.setDialogParent(&chatParent);
    coordinator.setProviderTestRunner(&runner);
    coordinator.ensureSettingsDialog();

    coordinator.runProviderTest(false);
    coordinator.fetchProviderModels();

    QCOMPARE(transportPtr->postCalls, 0);
    QCOMPARE(transportPtr->getCalls, 0);
    QCOMPARE(statusText, QStringLiteral("Ready"));
}

void SettingsCoordinatorTests::applySettingsFromDialogPropagatesConfigAndReportsSaveFailure() {
    BusyState state = BusyState::Idle;
    QString statusText = QStringLiteral("Ready");
    ais::config::AppConfig appliedConfig;
    int applyAppearanceCalls = 0;
    int rememberWindowSizesCalls = 0;
    int refreshHotkeysCalls = 0;
    int launchAtLoginCalls = 0;
    QWidget chatParent;

    SettingsCoordinator::Hooks hooks = makeHooks(state, statusText);
    hooks.onConfigApplied = [&](const ais::config::AppConfig& config) {
        appliedConfig = config;
    };
    hooks.applyAppearance = [&]() {
        applyAppearanceCalls += 1;
    };
    hooks.rememberWindowSizes = [&]() {
        rememberWindowSizesCalls += 1;
    };
    hooks.saveConfigSnapshot = []() {
        return false;
    };
    hooks.refreshHotkeys = [&]() {
        refreshHotkeysCalls += 1;
        return true;
    };
    hooks.applyLaunchAtLogin = [&]() {
        launchAtLoginCalls += 1;
        return true;
    };

    SettingsCoordinator coordinator(hooks);
    coordinator.setConfig(ais::config::AppConfig{});
    coordinator.setDialogParent(&chatParent);
    coordinator.ensureSettingsDialog();

    auto* dialog = chatParent.findChild<ais::ui::SettingsDialog*>();
    QVERIFY(dialog != nullptr);

    dialog->baseUrlField()->setText(QStringLiteral("https://example.com/v1"));
    dialog->apiKeyField()->setText(QStringLiteral("secret-key"));
    dialog->modelField()->setCurrentText(QStringLiteral("demo-model"));
    dialog->firstPromptField()->setPlainText(QStringLiteral("分析这张图"));
    dialog->launchAtLoginCheckBox()->setChecked(true);
    dialog->captureModeField()->setCurrentIndex(
        dialog->captureModeField()->findData(static_cast<int>(ais::capture::CaptureMode::HdrCompatible)));

    coordinator.applySettingsFromDialog();

    QCOMPARE(appliedConfig.activeProfile.baseUrl, QStringLiteral("https://example.com/v1"));
    QCOMPARE(appliedConfig.activeProfile.apiKey, QStringLiteral("secret-key"));
    QCOMPARE(appliedConfig.activeProfile.model, QStringLiteral("demo-model"));
    QCOMPARE(appliedConfig.firstPrompt, QStringLiteral("分析这张图"));
    QVERIFY(appliedConfig.launchAtLogin);
    QCOMPARE(static_cast<int>(appliedConfig.captureMode),
             static_cast<int>(ais::capture::CaptureMode::HdrCompatible));
    QCOMPARE(rememberWindowSizesCalls, 1);
    QCOMPARE(applyAppearanceCalls, 1);
    QCOMPARE(refreshHotkeysCalls, 1);
    QCOMPARE(launchAtLoginCalls, 1);
    QCOMPARE(statusText, QStringLiteral("Settings could not be saved"));
}

void SettingsCoordinatorTests::applySettingsFromDialogPreservesCombinedFailureStatus() {
    BusyState state = BusyState::Idle;
    QString statusText = QStringLiteral("Ready");
    QWidget chatParent;

    SettingsCoordinator::Hooks hooks = makeHooks(state, statusText);
    hooks.saveConfigSnapshot = []() {
        return true;
    };
    hooks.refreshHotkeys = []() {
        return false;
    };
    hooks.applyLaunchAtLogin = []() {
        return false;
    };

    SettingsCoordinator coordinator(hooks);
    coordinator.setConfig(ais::config::AppConfig{});
    coordinator.setDialogParent(&chatParent);
    coordinator.ensureSettingsDialog();

    coordinator.applySettingsFromDialog();

    QCOMPARE(statusText,
             QStringLiteral("Settings saved, but startup and hotkey registration failed"));
}

QTEST_MAIN(SettingsCoordinatorTests)

#include "test_settings_coordinator.moc"
