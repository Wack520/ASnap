#pragma once

#include <QList>
#include <QPixmap>
#include <QRect>

namespace ais::capture {

struct DesktopSnapshot {
    QPixmap displayImage;
    QPixmap captureImage;
    QRect virtualGeometry;
    QList<QRect> screenGeometries;
};

}  // namespace ais::capture
