#pragma once

#include <QList>
#include <QPixmap>
#include <QRect>

#include "capture/capture_pipeline_types.h"

namespace ais::capture {

struct ScreenMapping {
    QRect overlayRect;
    QRect virtualRect;
};

struct DesktopSnapshot {
    QPixmap displayImage;
    QPixmap captureImage;
    QRect overlayGeometry;
    QRect virtualGeometry;
    QList<ScreenMapping> screenMappings;
    CaptureDiagnostics diagnostics;
};

}  // namespace ais::capture
