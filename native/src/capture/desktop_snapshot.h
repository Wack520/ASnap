#pragma once

#include <QList>
#include <QPixmap>
#include <QRect>

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
};

}  // namespace ais::capture
