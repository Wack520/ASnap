#pragma once

#include <QMetaType>
#include <QPixmap>
#include <QRect>

namespace ais::capture {

struct CaptureSelection {
    QPixmap image;
    QRect localRect;
    QRect virtualRect;
};

}  // namespace ais::capture

Q_DECLARE_METATYPE(ais::capture::CaptureSelection)
