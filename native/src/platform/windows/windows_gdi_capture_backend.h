#pragma once

#include <optional>

#include "capture/capture_pipeline_types.h"

namespace ais::platform::windows {

[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureDisplayWithGdi(
    const ais::capture::DisplayDescriptor& display);

}  // namespace ais::platform::windows
