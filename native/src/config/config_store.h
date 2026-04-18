#pragma once

#include <QString>

#include "config/app_config.h"

namespace ais::config {

class ConfigStore {
public:
    explicit ConfigStore(QString filePath);

    [[nodiscard]] const QString& filePath() const noexcept { return filePath_; }

    [[nodiscard]] AppConfig load() const;
    [[nodiscard]] bool save(const AppConfig& config) const;

private:
    QString filePath_;
};

}  // namespace ais::config
