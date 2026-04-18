#include "platform/windows/startup_registry.h"

#include <QDir>
#include <QSettings>

namespace ais::platform::windows {

namespace {

constexpr auto kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kStartupValueName = "AiScreenshotTool";

[[nodiscard]] QString quotedExecutablePath(const QString& executablePath) {
    const QString nativePath = QDir::toNativeSeparators(executablePath.trimmed());
    if (nativePath.isEmpty()) {
        return {};
    }
    return QStringLiteral("\"%1\"").arg(nativePath);
}

}  // namespace

bool WindowsStartupRegistry::setLaunchAtLogin(const bool enabled, const QString& executablePath) {
    QSettings settings(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);

    if (!enabled) {
        settings.remove(QString::fromLatin1(kStartupValueName));
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    const QString startupCommand = quotedExecutablePath(executablePath);
    if (startupCommand.isEmpty()) {
        return false;
    }

    settings.setValue(QString::fromLatin1(kStartupValueName), startupCommand);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

}  // namespace ais::platform::windows
