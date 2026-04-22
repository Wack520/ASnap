#pragma once

namespace ais::capture {

enum class ScreenCaptureBackend {
    WindowsGraphicsCaptureBgra,
    WindowsGraphicsCaptureFp16,
    Gdi,
};

}  // namespace ais::capture
