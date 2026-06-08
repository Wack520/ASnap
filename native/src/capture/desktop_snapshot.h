#pragma once

#include <QList>
#include <QPixmap>
#include <QRect>

#include "capture/capture_pipeline_types.h"

namespace ais::capture {

struct ScreenMapping {
    QRect overlayRect;
    QRect virtualRect;
    QRect captureRect;
    qreal captureDevicePixelRatio = 1.0;
};

struct DesktopSnapshot {
    QPixmap displayImage;
    QPixmap captureImage;
    QRect overlayGeometry;
    QRect virtualGeometry;
    QRect captureGeometry;
    QList<ScreenMapping> screenMappings;
    CaptureDiagnostics diagnostics;
};

}  // namespace ais::capture
