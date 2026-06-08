#pragma once

#include <QList>

#include "capture/capture_mode.h"
#include "capture/capture_pipeline_types.h"

namespace ais::capture {

class ScreenCaptureBackend {
public:
    virtual ~ScreenCaptureBackend() = default;

    [[nodiscard]] virtual QList<RawScreenFrame> captureDisplays(
        const QList<DisplayDescriptor>& displays,
        CaptureDiagnostics* diagnostics,
        CaptureMode captureMode) const = 0;
};

}  // namespace ais::capture
