#pragma once

#include <memory>

#include <QPoint>
#include <QImage>
#include <QList>
#include <QRect>

#include "capture/desktop_snapshot.h"
#include "capture/display_topology.h"
#include "capture/screen_capture_backend.h"

namespace ais::capture {

struct CapturedScreenFrame {
    QImage image;
    QRect overlayGeometry;
    QRect virtualGeometry;
    qreal devicePixelRatio = 1.0;
};

class DesktopCaptureService {
public:
    DesktopCaptureService();
    DesktopCaptureService(std::unique_ptr<DisplayTopology> topology,
                          std::unique_ptr<ScreenCaptureBackend> backend);

    [[nodiscard]] DesktopSnapshot captureVirtualDesktop() const;
    // Compatibility wrappers around SnapshotComposer for existing callers.
    [[nodiscard]] static DesktopSnapshot composeFrames(const QList<CapturedScreenFrame>& frames);
    // Compatibility wrapper; FrameNormalizer owns HDR/SDR normalization behavior.
    [[nodiscard]] static QImage normalizeForSdr(const QImage& image);
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
