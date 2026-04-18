#pragma once

#include <QString>
#include <QImage>
#include <QList>
#include <QRect>

namespace ais::platform::windows {

struct NativeScreenFrame {
    QString deviceName;
    QImage image;
    QRect monitorRect;
};

[[nodiscard]] QList<NativeScreenFrame> captureNativeScreens();

}  // namespace ais::platform::windows
