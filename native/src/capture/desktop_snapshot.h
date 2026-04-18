#pragma once

#include <QPixmap>
#include <QRect>

namespace ais::capture {

struct DesktopSnapshot {
    QPixmap displayImage;
    QPixmap captureImage;
    QRect virtualGeometry;
};

}  // namespace ais::capture
