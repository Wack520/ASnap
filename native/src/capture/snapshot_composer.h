#pragma once

#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QRect>

#include "capture/capture_pipeline_types.h"
#include "capture/desktop_snapshot.h"

namespace ais::capture {

class SnapshotComposer final {
public:
    [[nodiscard]] static DesktopSnapshot composeFrames(const QList<PreparedScreenFrame>& frames,
                                                       const CaptureDiagnostics& diagnostics = {});
    [[nodiscard]] static DesktopSnapshot snapshotForScreen(const DesktopSnapshot& snapshot,
                                                           const ScreenMapping& screenMapping);
    [[nodiscard]] static QRect translateToVirtual(const QRect& localRect,
                                                  const QPoint& virtualOrigin);
    [[nodiscard]] static QRect translateToVirtual(const DesktopSnapshot& snapshot,
                                                  const QRect& localRect);
    [[nodiscard]] static QPixmap copyLogicalSelection(const QPixmap& source,
                                                      const QRect& logicalRect);
};

}  // namespace ais::capture
