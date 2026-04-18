#pragma once

#include <QString>
#include <QImage>
#include <QList>

namespace ais::platform::windows {

struct NativeScreenFrame {
    QString deviceName;
    QImage image;
};

[[nodiscard]] QList<NativeScreenFrame> captureNativeScreens();

}  // namespace ais::platform::windows
