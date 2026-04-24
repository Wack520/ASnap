#pragma once

#include <QString>

namespace ais::platform::windows {

[[nodiscard]] QString querySelectedText(QString* errorMessage = nullptr);

}  // namespace ais::platform::windows
