#pragma once

#include <optional>

#include <QString>

#include "capture/screen_capture_backend.h"

namespace ais::platform::windows {

namespace detail {

using WgcCaptureFunction = std::optional<ais::capture::RawScreenFrame> (*)(
    const ais::capture::DisplayDescriptor& display,
    ais::capture::CaptureBackendKind preferredKind,
    QString* failureNote);
using GdiCaptureFunction = std::optional<ais::capture::RawScreenFrame> (*)(
    const ais::capture::DisplayDescriptor& display);

struct WindowsCaptureBackendTestHooks {
    WgcCaptureFunction captureWithWgc = nullptr;
    GdiCaptureFunction captureWithGdi = nullptr;
};

extern const WindowsCaptureBackendTestHooks* g_windowsCaptureBackendTestHooks;

void setWindowsCaptureBackendTestHooks(const WindowsCaptureBackendTestHooks* hooks);

}  // namespace detail

class WindowsScreenCaptureBackend final : public ais::capture::ScreenCaptureBackend {
public:
    [[nodiscard]] QList<ais::capture::RawScreenFrame> captureDisplays(
        const QList<ais::capture::DisplayDescriptor>& displays,
        ais::capture::CaptureDiagnostics* diagnostics,
        ais::capture::CaptureMode captureMode) const override;
};

}  // namespace ais::platform::windows
