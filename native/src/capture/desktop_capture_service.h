#pragma once

#include <QPoint>
#include <QRect>
#include <QImage>
#include <QList>

#include "capture/desktop_snapshot.h"

namespace ais::capture {

struct CapturedScreenFrame {
    QImage image;
    QRect geometry;
    qreal devicePixelRatio = 1.0;
};

class DesktopCaptureService {
public:
    [[nodiscard]] DesktopSnapshot captureVirtualDesktop() const;
    [[nodiscard]] static DesktopSnapshot composeFrames(const QList<CapturedScreenFrame>& frames);
    [[nodiscard]] static QImage normalizeForSdr(const QImage& image);
    [[nodiscard]] static QRect translateToVirtual(const QRect& localRect,
                                                  const QPoint& virtualOrigin);
    [[nodiscard]] static QPixmap copyLogicalSelection(const QPixmap& source,
                                                      const QRect& logicalRect);
};

}  // namespace ais::capture
