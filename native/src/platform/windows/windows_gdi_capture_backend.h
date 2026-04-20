#pragma once

#include <optional>

#include <QRect>

#include <windows.h>

#include "capture/capture_pipeline_types.h"

namespace ais::platform::windows {

namespace detail {

[[nodiscard]] RECT makeWinRectForGdiCapture(const QRect& rect);

}  // namespace detail

[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureDisplayWithGdi(
    const ais::capture::DisplayDescriptor& display);

}  // namespace ais::platform::windows
