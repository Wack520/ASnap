#pragma once

#include <QString>

namespace ais::platform::windows {

class StartupRegistry {
public:
    virtual ~StartupRegistry() = default;
    [[nodiscard]] virtual bool setLaunchAtLogin(bool enabled, const QString& executablePath) = 0;
};

class WindowsStartupRegistry final : public StartupRegistry {
public:
    [[nodiscard]] bool setLaunchAtLogin(bool enabled, const QString& executablePath) override;
};

}  // namespace ais::platform::windows
