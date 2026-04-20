#pragma once

#include <memory>

#include <QPoint>
#include <QList>
#include <QRect>

#include "capture/desktop_snapshot.h"
#include "capture/display_topology.h"
#include "capture/screen_capture_backend.h"

namespace ais::capture {

class DesktopCaptureService {
public:
    DesktopCaptureService();
    DesktopCaptureService(std::unique_ptr<DisplayTopology> topology,
                          std::unique_ptr<ScreenCaptureBackend> backend);

    [[nodiscard]] DesktopSnapshot captureVirtualDesktop() const;
    [[nodiscard]] static QRect translateToVirtual(const QRect& localRect,
                                                  const QPoint& virtualOrigin);
    [[nodiscard]] static QRect translateToVirtual(const DesktopSnapshot& snapshot,
                                                  const QRect& localRect);
    [[nodiscard]] static QPixmap copyLogicalSelection(const QPixmap& source,
                                                      const QRect& logicalRect);

private:
    std::unique_ptr<DisplayTopology> topology_;
    std::unique_ptr<ScreenCaptureBackend> backend_;
};

}  // namespace ais::capture
