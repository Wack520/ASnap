#include <QAction>
#include <QWidget>
#include <QtTest/QtTest>

#include "app/shell_integration.h"
#include "config/app_config.h"

using ais::app::ShellIntegration;

class ShellIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void hotkeyRegistrationRefreshUsesAdapters();
    void launchAtLoginPreferenceUsesAdapter();
    void initializeWiresTrayActionsToHooks();
    void quitReleaseUnregistersHotkeys();
};

void ShellIntegrationTests::hotkeyRegistrationRefreshUsesAdapters() {
    int aiRegisterCount = 0;
    int plainRegisterCount = 0;
    QString aiShortcut;
    QString plainShortcut;

    ShellIntegration integration;
    ShellIntegration::Adapters adapters;
    adapters.registerAiHotkey = [&](const QString& shortcut) {
        aiRegisterCount += 1;
        aiShortcut = shortcut;
        return true;
    };
    adapters.registerPlainHotkey = [&](const QString& shortcut) {
        plainRegisterCount += 1;
        plainShortcut = shortcut;
        return false;
    };
    integration.setAdaptersForTest(std::move(adapters));

    ais::config::AppConfig config;
    config.aiShortcut = QStringLiteral("Ctrl+Alt+A");
    config.screenshotShortcut = QStringLiteral("Ctrl+Alt+S");

    QVERIFY(!integration.refreshHotkeys(config));
    QCOMPARE(aiRegisterCount, 1);
    QCOMPARE(plainRegisterCount, 1);
    QCOMPARE(aiShortcut, QStringLiteral("Ctrl+Alt+A"));
    QCOMPARE(plainShortcut, QStringLiteral("Ctrl+Alt+S"));
}

void ShellIntegrationTests::launchAtLoginPreferenceUsesAdapter() {
    bool capturedEnabled = false;
    QString capturedPath;

    ShellIntegration integration;
    ShellIntegration::Adapters adapters;
    adapters.setLaunchAtLogin = [&](const bool enabled, const QString& path) {
        capturedEnabled = enabled;
        capturedPath = path;
        return true;
    };
    integration.setAdaptersForTest(std::move(adapters));

    ais::config::AppConfig config;
    config.launchAtLogin = true;

    QVERIFY(integration.applyLaunchAtLoginPreference(config));
    QVERIFY(capturedEnabled);
    QVERIFY(!capturedPath.isEmpty());
}

void ShellIntegrationTests::initializeWiresTrayActionsToHooks() {
    int aiCount = 0;
    int plainCount = 0;
    int settingsCount = 0;
    QWidget menuParent;

    ShellIntegration::Hooks hooks;
    hooks.onAiCaptureRequested = [&]() {
        aiCount += 1;
    };
    hooks.onPlainCaptureRequested = [&]() {
        plainCount += 1;
    };
    hooks.onSettingsRequested = [&]() {
        settingsCount += 1;
    };

    ShellIntegration integration(std::move(hooks));
    ShellIntegration::Adapters adapters;
    adapters.registerAiHotkey = [](const QString&) {
        return true;
    };
    adapters.registerPlainHotkey = [](const QString&) {
        return true;
    };
    adapters.unregisterAiHotkey = []() {};
    adapters.unregisterPlainHotkey = []() {};
    integration.setAdaptersForTest(std::move(adapters));

    QVERIFY(integration.initialize(&menuParent));

    const auto actions = menuParent.findChildren<QAction*>();
    QAction* aiAction = nullptr;
    QAction* plainAction = nullptr;
    QAction* settingsAction = nullptr;
    for (QAction* action : actions) {
        if (action == nullptr) {
            continue;
        }

        if (action->text() == QStringLiteral("AI 截图")) {
            aiAction = action;
        } else if (action->text() == QStringLiteral("普通截图")) {
            plainAction = action;
        } else if (action->text() == QStringLiteral("设置")) {
            settingsAction = action;
        }
    }

    QVERIFY(aiAction != nullptr);
    QVERIFY(plainAction != nullptr);
    QVERIFY(settingsAction != nullptr);

    aiAction->trigger();
    plainAction->trigger();
    settingsAction->trigger();

    QCOMPARE(aiCount, 1);
    QCOMPARE(plainCount, 1);
    QCOMPARE(settingsCount, 1);
}

void ShellIntegrationTests::quitReleaseUnregistersHotkeys() {
    int quitCount = 0;
    int aiUnregisterCount = 0;
    int plainUnregisterCount = 0;

    ShellIntegration::Hooks hooks;
    hooks.onQuitRequested = [&]() {
        quitCount += 1;
    };

    ShellIntegration integration(std::move(hooks));
    ShellIntegration::Adapters adapters;
    adapters.unregisterAiHotkey = [&]() {
        aiUnregisterCount += 1;
    };
    adapters.unregisterPlainHotkey = [&]() {
        plainUnregisterCount += 1;
    };
    integration.setAdaptersForTest(std::move(adapters));

    integration.requestQuitForTest();
    integration.releaseResources();
    integration.releaseResources();

    QCOMPARE(quitCount, 1);
    QCOMPARE(aiUnregisterCount, 1);
    QCOMPARE(plainUnregisterCount, 1);
}

QTEST_MAIN(ShellIntegrationTests)

#include "test_shell_integration.moc"
