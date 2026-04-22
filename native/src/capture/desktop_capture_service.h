#pragma once

#include <QPoint>
#include <QRect>
#include <QImage>
#include <QList>
#include <QString>

#include "capture/capture_mode.h"
#include "capture/desktop_snapshot.h"
#include "platform/windows/native_screen_capture.h"

namespace ais::capture {

struct CapturedScreenFrame {
    QImage image;
    QRect overlayGeometry;
    QRect virtualGeometry;
    qreal devicePixelRatio = 1.0;
};

namespace detail {

struct ScreenTarget {
    QString name;
    QRect logicalGeometry;
    qreal devicePixelRatio = 1.0;
};

[[nodiscard]] QList<CapturedScreenFrame> mapNativeFramesToScreenTargets(
    const QList<ais::platform::windows::NativeScreenFrame>& nativeFrames,
    const QList<ScreenTarget>& screenTargets);

}  // namespace detail

class DesktopCaptureService {
public:
    void setCaptureMode(CaptureMode mode) noexcept { captureMode_ = mode; }
    [[nodiscard]] CaptureMode captureMode() const noexcept { return captureMode_; }

    [[nodiscard]] DesktopSnapshot captureVirtualDesktop() const;
    [[nodiscard]] static DesktopSnapshot composeFrames(const QList<CapturedScreenFrame>& frames);
    [[nodiscard]] static QImage normalizeForSdr(const QImage& image);
    [[nodiscard]] static DesktopSnapshot snapshotForScreen(const DesktopSnapshot& snapshot,
                                                          const ScreenMapping& screenMapping);
    [[nodiscard]] static QRect translateToVirtual(const QRect& localRect,
                                                  const QPoint& virtualOrigin);
    [[nodiscard]] static QRect translateToVirtual(const DesktopSnapshot& snapshot,
                                                  const QRect& localRect);
    [[nodiscard]] static QPixmap copyLogicalSelection(const QPixmap& source,
                                                      const QRect& logicalRect);

private:
    CaptureMode captureMode_ = CaptureMode::Standard;
};

}  // namespace ais::capture
