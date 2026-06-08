#include "capture/snapshot_composer.h"

#include <QColorSpace>
#include <QImage>
#include <QPainter>

namespace ais::capture {

namespace {

[[nodiscard]] QRect virtualRectFor(const ScreenMapping& mapping) {
    return mapping.virtualRect.isValid() ? mapping.virtualRect : mapping.overlayRect;
}

[[nodiscard]] QRect captureRectFor(const ScreenMapping& mapping) {
    return mapping.captureRect.isValid() ? mapping.captureRect : mapping.overlayRect;
}

[[nodiscard]] QRect virtualRectFor(const DisplayDescriptor& display) {
    return display.virtualRect.isValid() ? display.virtualRect : display.monitorRect;
}

[[nodiscard]] QRect overlayRectFor(const DisplayDescriptor& display) {
    return virtualRectFor(display);
}

[[nodiscard]] qreal captureDevicePixelRatioFor(const ScreenMapping& mapping) {
    if (mapping.captureDevicePixelRatio > 0.0) {
        return mapping.captureDevicePixelRatio;
    }

    const QRect overlayRect = mapping.overlayRect;
    const QRect captureRect = captureRectFor(mapping);
    if (overlayRect.width() <= 0 || overlayRect.height() <= 0) {
        return 1.0;
    }

    const qreal scaleX = static_cast<qreal>(captureRect.width()) /
                         static_cast<qreal>(overlayRect.width());
    const qreal scaleY = static_cast<qreal>(captureRect.height()) /
                         static_cast<qreal>(overlayRect.height());
    return qMax<qreal>(1.0, qMax(scaleX, scaleY));
}

[[nodiscard]] QRect captureLocalRectFor(const DesktopSnapshot& snapshot,
                                        const QRect& overlayGlobalRect,
                                        const ScreenMapping& mapping) {
    const QRect overlayRect = mapping.overlayRect;
    const QRect captureRect = captureRectFor(mapping);
    const QRect intersection = overlayGlobalRect.intersected(overlayRect);
    if (!intersection.isValid() || intersection.isEmpty() || !captureRect.isValid() ||
        captureRect.isEmpty()) {
        return {};
    }

    const qreal scaleX = overlayRect.width() > 0
                             ? static_cast<qreal>(captureRect.width()) /
                                   static_cast<qreal>(overlayRect.width())
                             : 1.0;
    const qreal scaleY = overlayRect.height() > 0
                             ? static_cast<qreal>(captureRect.height()) /
                                   static_cast<qreal>(overlayRect.height())
                             : 1.0;
    const int relativeLeft = intersection.left() - overlayRect.left();
    const int relativeTop = intersection.top() - overlayRect.top();
    const QRect mappedCaptureRect(captureRect.left() + qRound(relativeLeft * scaleX),
                                  captureRect.top() + qRound(relativeTop * scaleY),
                                  qMax(1, qRound(intersection.width() * scaleX)),
                                  qMax(1, qRound(intersection.height() * scaleY)));
    const QRect captureGeometry =
        snapshot.captureGeometry.isValid() ? snapshot.captureGeometry : snapshot.overlayGeometry;
    return mappedCaptureRect.translated(-captureGeometry.topLeft());
}

}  // namespace

DesktopSnapshot SnapshotComposer::composeFrames(const QList<PreparedScreenFrame>& frames,
                                                const CaptureDiagnostics& diagnostics) {
    QRect overlayGeometry;
    QRect virtualGeometry;
    QRect captureGeometry;
    bool first = true;
    QList<PreparedScreenFrame> validFrames;
    QList<ScreenMapping> screenMappings;

    for (const PreparedScreenFrame& frame : frames) {
        if (frame.normalizedImage.isNull() || !frame.display.monitorRect.isValid() ||
            frame.display.monitorRect.isEmpty()) {
            continue;
        }

        const QRect overlayRect = overlayRectFor(frame.display);
        const QRect frameVirtualGeometry = virtualRectFor(frame.display);
        validFrames.append(frame);
        screenMappings.append(ScreenMapping{
            .overlayRect = overlayRect,
            .virtualRect = frameVirtualGeometry,
            .captureRect = frame.display.monitorRect,
            .captureDevicePixelRatio = qMax<qreal>(1.0, frame.display.devicePixelRatio),
        });
        overlayGeometry = first ? overlayRect : overlayGeometry.united(overlayRect);
        virtualGeometry = first ? frameVirtualGeometry : virtualGeometry.united(frameVirtualGeometry);
        captureGeometry =
            first ? frame.display.monitorRect : captureGeometry.united(frame.display.monitorRect);
        first = false;
    }

    if (validFrames.isEmpty() || overlayGeometry.isNull() || overlayGeometry.isEmpty()) {
        return DesktopSnapshot{
            .diagnostics = diagnostics,
        };
    }

    QImage displayCanvas(QSize(qMax(1, overlayGeometry.width()), qMax(1, overlayGeometry.height())),
                         QImage::Format_ARGB32_Premultiplied);
    displayCanvas.setColorSpace(QColorSpace(QColorSpace::SRgb));
    displayCanvas.fill(Qt::black);

    QPainter displayPainter(&displayCanvas);
    displayPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const PreparedScreenFrame& frame : validFrames) {
        const QRect overlayRect = overlayRectFor(frame.display);
        const QPoint topLeft = overlayRect.topLeft() - overlayGeometry.topLeft();
        const QRect targetRect(topLeft, overlayRect.size());
        displayPainter.drawImage(targetRect, frame.normalizedImage);
    }
    displayPainter.end();

    QImage captureCanvas(QSize(qMax(1, captureGeometry.width()), qMax(1, captureGeometry.height())),
                         QImage::Format_ARGB32_Premultiplied);
    captureCanvas.setColorSpace(QColorSpace(QColorSpace::SRgb));
    captureCanvas.fill(Qt::black);

    QPainter capturePainter(&captureCanvas);
    capturePainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const PreparedScreenFrame& frame : validFrames) {
        const QPoint topLeft = frame.display.monitorRect.topLeft() - captureGeometry.topLeft();
        const QRect targetRect(topLeft, frame.display.monitorRect.size());
        capturePainter.drawImage(targetRect, frame.normalizedImage);
    }
    capturePainter.end();

    return DesktopSnapshot{
        .displayImage = QPixmap::fromImage(displayCanvas),
        .captureImage = QPixmap::fromImage(captureCanvas),
        .overlayGeometry = overlayGeometry,
        .virtualGeometry = virtualGeometry,
        .captureGeometry = captureGeometry,
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
    const QRect captureLocalRect =
        captureLocalRectFor(snapshot, screenMapping.overlayRect, screenMapping);
    QPixmap capturePixmap;
    if (!snapshot.captureImage.isNull() && captureLocalRect.isValid() && !captureLocalRect.isEmpty()) {
        capturePixmap = QPixmap::fromImage(snapshot.captureImage.toImage().copy(captureLocalRect));
        capturePixmap.setDevicePixelRatio(captureDevicePixelRatioFor(screenMapping));
    }
    if (capturePixmap.isNull()) {
        const QPixmap captureSource =
            snapshot.captureImage.isNull() ? snapshot.displayImage : snapshot.captureImage;
        capturePixmap = copyLogicalSelection(captureSource, localRect);
    }

    return DesktopSnapshot{
        .displayImage = copyLogicalSelection(displaySource, localRect),
        .captureImage = capturePixmap,
        .overlayGeometry = virtualRect,
        .virtualGeometry = virtualRect,
        .captureGeometry = captureRectFor(screenMapping),
        .screenMappings = {ScreenMapping{
            .overlayRect = virtualRect,
            .virtualRect = virtualRect,
            .captureRect = captureRectFor(screenMapping),
            .captureDevicePixelRatio = captureDevicePixelRatioFor(screenMapping),
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

    const QImage sourceImage = source.toImage();
    QPixmap cropped = QPixmap::fromImage(sourceImage.copy(physicalRect));
    cropped.setDevicePixelRatio(devicePixelRatio);
    return cropped;
}

}  // namespace ais::capture
