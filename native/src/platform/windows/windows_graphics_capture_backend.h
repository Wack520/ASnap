#pragma once

#include "capture/capture_mode.h"
#include "capture/screen_capture_backend.h"
#include "platform/windows/native_screen_capture.h"

#include <QSize>

namespace ais::platform::windows {

namespace detail {

enum class MappedTextureFormat {
    Bgra8Unorm,
    Rgba16Float,
};

struct StillCaptureSessionOptions {
    bool borderRequired = false;
    bool cursorCaptureEnabled = false;
};

[[nodiscard]] QImage makeQImageFromMappedTexture(MappedTextureFormat format,
                                                 const QSize& size,
                                                 qsizetype rowPitch,
                                                 const uchar* data);
[[nodiscard]] StillCaptureSessionOptions stillCaptureSessionOptions();

}  // namespace detail

[[nodiscard]] bool isWindowsGraphicsCaptureSupported();
[[nodiscard]] QList<NativeScreenFrame> captureWindowsGraphicsScreens();
[[nodiscard]] QList<NativeScreenFrame> captureWindowsScreens(capture::CaptureMode mode);
[[nodiscard]] QList<capture::ScreenCaptureBackend> backendOrderForCaptureMode(
    capture::CaptureMode mode);

}  // namespace ais::platform::windows
