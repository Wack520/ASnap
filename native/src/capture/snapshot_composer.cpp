#include "capture/snapshot_composer.h"

#include <QColorSpace>
#include <QImage>
#include <QPainter>

#include "capture/frame_normalizer.h"

namespace ais::capture {

namespace {

[[nodiscard]] QRect virtualRectFor(const ScreenMapping& mapping) {
    return mapping.virtualRect.isValid() ? mapping.virtualRect : mapping.overlayRect;
}

[[nodiscard]] QRect virtualRectFor(const DisplayDescriptor& display) {
    return display.virtualRect.isValid() ? display.virtualRect : display.monitorRect;
}

}  // namespace

DesktopSnapshot SnapshotComposer::composeFrames(const QList<PreparedScreenFrame>& frames,
                                                const CaptureDiagnostics& diagnostics) {
    QRect overlayGeometry;
    QRect virtualGeometry;
    bool first = true;
    QList<PreparedScreenFrame> validFrames;
    QList<ScreenMapping> screenMappings;

    for (const PreparedScreenFrame& frame : frames) {
        if (frame.normalizedImage.isNull() || !frame.display.monitorRect.isValid() ||
            frame.display.monitorRect.isEmpty()) {
            continue;
        }

        validFrames.append(frame);
        screenMappings.append(ScreenMapping{
            .overlayRect = frame.display.monitorRect,
            .virtualRect = virtualRectFor(frame.display),
        });
        overlayGeometry =
            first ? frame.display.monitorRect : overlayGeometry.united(frame.display.monitorRect);
        const QRect frameVirtualGeometry = virtualRectFor(frame.display);
        virtualGeometry =
            first ? frameVirtualGeometry : virtualGeometry.united(frameVirtualGeometry);
        first = false;
    }

    if (validFrames.isEmpty() || overlayGeometry.isNull() || overlayGeometry.isEmpty()) {
        return DesktopSnapshot{
            .diagnostics = diagnostics,
        };
    }

    QImage canvas(QSize(qMax(1, overlayGeometry.width()), qMax(1, overlayGeometry.height())),
                  QImage::Format_ARGB32_Premultiplied);
    canvas.setColorSpace(QColorSpace(QColorSpace::SRgb));
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const PreparedScreenFrame& frame : validFrames) {
        const QPoint topLeft = frame.display.monitorRect.topLeft() - overlayGeometry.topLeft();
        const QRect targetRect(topLeft, frame.display.monitorRect.size());
        painter.drawImage(targetRect, frame.normalizedImage);
    }
    painter.end();

    QPixmap stitched = QPixmap::fromImage(canvas);
    return DesktopSnapshot{
        .displayImage = stitched,
        .captureImage = stitched,
        .overlayGeometry = overlayGeometry,
        .virtualGeometry = virtualGeometry,
        .screenMappings = screenMappings,
        .diagnostics = diagnostics,
    };
}

DesktopSnapshot SnapshotComposer::snapshotForScreen(const DesktopSnapshot& snapshot,
                                                    const ScreenMapping& screenMapping) {
    if (!screenMapping.overlayRect.isValid() || screenMapping.overlayRect.isEmpty()) {
        return {};
    }

    const QPoint snapshotOrigin = snapshot.overlayGeometry.isValid()
                                      ? snapshot.overlayGeometry.topLeft()
                                      : snapshot.virtualGeometry.topLeft();
    const QRect localRect = screenMapping.overlayRect.translated(-snapshotOrigin);
    const QRect virtualRect = virtualRectFor(screenMapping);
    const QPixmap displaySource =
        snapshot.displayImage.isNull() ? snapshot.captureImage : snapshot.displayImage;
    const QPixmap captureSource =
        snapshot.captureImage.isNull() ? snapshot.displayImage : snapshot.captureImage;

    return DesktopSnapshot{
        .displayImage = copyLogicalSelection(displaySource, localRect),
        .captureImage = copyLogicalSelection(captureSource, localRect),
        .overlayGeometry = screenMapping.overlayRect,
        .virtualGeometry = virtualRect,
        .screenMappings = {ScreenMapping{
            .overlayRect = screenMapping.overlayRect,
            .virtualRect = virtualRect,
        }},
        .diagnostics = snapshot.diagnostics,
    };
}

QRect SnapshotComposer::translateToVirtual(const QRect& localRect,
                                           const QPoint& virtualOrigin) {
    return localRect.translated(virtualOrigin);
}

QRect SnapshotComposer::translateToVirtual(const DesktopSnapshot& snapshot,
                                           const QRect& localRect) {
    if (!localRect.isValid() || localRect.isEmpty()) {
        return {};
    }

    if (snapshot.screenMappings.isEmpty() || !snapshot.overlayGeometry.isValid()) {
        return translateToVirtual(localRect, snapshot.virtualGeometry.topLeft());
    }

    const QRect globalOverlayRect = localRect.translated(snapshot.overlayGeometry.topLeft());
    QRect mappedVirtualRect;
    bool first = true;

    for (const ScreenMapping& mapping : snapshot.screenMappings) {
        const QRect overlayRect = mapping.overlayRect;
        const QRect virtualRect = virtualRectFor(mapping);
        const QRect intersection = globalOverlayRect.intersected(overlayRect);
        if (!intersection.isValid() || intersection.isEmpty()) {
            continue;
        }

        const double scaleX = overlayRect.width() > 0
                                  ? static_cast<double>(virtualRect.width()) /
                                        static_cast<double>(overlayRect.width())
                                  : 1.0;
        const double scaleY = overlayRect.height() > 0
                                  ? static_cast<double>(virtualRect.height()) /
                                        static_cast<double>(overlayRect.height())
                                  : 1.0;

        const int relativeLeft = intersection.left() - overlayRect.left();
        const int relativeTop = intersection.top() - overlayRect.top();
        const int mappedLeft = virtualRect.left() + qRound(relativeLeft * scaleX);
        const int mappedTop = virtualRect.top() + qRound(relativeTop * scaleY);
        const int mappedWidth = qMax(1, qRound(intersection.width() * scaleX));
        const int mappedHeight = qMax(1, qRound(intersection.height() * scaleY));
        const QRect mappedRect(mappedLeft, mappedTop, mappedWidth, mappedHeight);

        mappedVirtualRect = first ? mappedRect : mappedVirtualRect.united(mappedRect);
        first = false;
    }

    if (!first) {
        return mappedVirtualRect;
    }

    return translateToVirtual(localRect, snapshot.virtualGeometry.topLeft());
}

QPixmap SnapshotComposer::copyLogicalSelection(const QPixmap& source,
                                               const QRect& logicalRect) {
    if (source.isNull() || !logicalRect.isValid() || logicalRect.isEmpty()) {
        return {};
    }

    const qreal devicePixelRatio = qMax(1.0, source.devicePixelRatio());
    const QRect physicalRect(qRound(logicalRect.x() * devicePixelRatio),
                             qRound(logicalRect.y() * devicePixelRatio),
                             qRound(logicalRect.width() * devicePixelRatio),
                             qRound(logicalRect.height() * devicePixelRatio));

    QPixmap cropped = source.copy(physicalRect);
    cropped = QPixmap::fromImage(FrameNormalizer::normalizeToSdr(cropped.toImage()));
    cropped.setDevicePixelRatio(devicePixelRatio);
    return cropped;
}

}  // namespace ais::capture
