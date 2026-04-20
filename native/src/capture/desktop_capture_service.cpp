#include "capture/desktop_capture_service.h"

#include <memory>

#include "capture/frame_normalizer.h"
#include "capture/snapshot_composer.h"
#include "platform/windows/windows_capture_backend.h"
#include "platform/windows/windows_display_topology.h"

namespace ais::capture {

namespace {

[[nodiscard]] QString normalizedScreenName(QString name) {
    name = name.trimmed().toUpper();
    if (name.startsWith(QStringLiteral(R"(\\?\)"))) {
        name.remove(0, 4);
    }
    if (name.startsWith(QStringLiteral(R"(\\.\)"))) {
        name.remove(0, 4);
    }
    return name;
}

[[nodiscard]] bool isHdrLikeImageFormat(const QImage& image) {
    switch (image.format()) {
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return true;
    default:
        return false;
    }
}

void markToneMappedDiagnostics(const QList<PreparedScreenFrame>& preparedFrames,
                               CaptureDiagnostics* diagnostics) {
    if (diagnostics == nullptr || diagnostics->entries.isEmpty()) {
        return;
    }

    for (const PreparedScreenFrame& frame : preparedFrames) {
        if (!frame.hdrToneMapped) {
            continue;
        }

        for (CaptureDiagnosticsEntry& entry : diagnostics->entries) {
            if (normalizedScreenName(entry.deviceName) ==
                normalizedScreenName(frame.display.deviceName)) {
                entry.hdrToneMapped = true;
                break;
            }
        }
    }
}

[[nodiscard]] QList<PreparedScreenFrame> preparedFramesFromCaptured(
    const QList<CapturedScreenFrame>& frames) {
    QList<PreparedScreenFrame> prepared;
    prepared.reserve(frames.size());

    for (const CapturedScreenFrame& frame : frames) {
        prepared.append(PreparedScreenFrame{
            .display = DisplayDescriptor{
                .monitorRect = frame.overlayGeometry,
                .virtualRect = frame.virtualGeometry.isValid() ? frame.virtualGeometry
                                                               : frame.overlayGeometry,
                .devicePixelRatio = frame.devicePixelRatio,
            },
            .normalizedImage = frame.image,
        });
    }

    return prepared;
}

[[nodiscard]] QList<PreparedScreenFrame> preparedFramesFromRaw(
    const QList<RawScreenFrame>& frames) {
    QList<PreparedScreenFrame> prepared;
    prepared.reserve(frames.size());

    for (const RawScreenFrame& frame : frames) {
        if (frame.image.isNull()) {
            continue;
        }

        const bool hdrToneMapped = frame.isHdrLike || isHdrLikeImageFormat(frame.image);
        prepared.append(PreparedScreenFrame{
            .display = frame.display,
            .normalizedImage = DesktopCaptureService::normalizeForSdr(frame.image),
            .backendKind = frame.backendKind,
            .hdrToneMapped = hdrToneMapped,
        });
    }

    return prepared;
}

}  // namespace

DesktopCaptureService::DesktopCaptureService()
    : DesktopCaptureService(std::make_unique<platform::windows::WindowsDisplayTopology>(),
                            std::make_unique<platform::windows::WindowsScreenCaptureBackend>()) {}

DesktopCaptureService::DesktopCaptureService(std::unique_ptr<DisplayTopology> topology,
                                             std::unique_ptr<ScreenCaptureBackend> backend)
    : topology_(std::move(topology)),
      backend_(std::move(backend)) {}

DesktopSnapshot DesktopCaptureService::captureVirtualDesktop() const {
    if (!topology_ || !backend_) {
        return {};
    }

    CaptureDiagnostics diagnostics;
    const QList<DisplayDescriptor> displays = topology_->enumerateDisplays();
    const QList<RawScreenFrame> rawFrames = backend_->captureDisplays(displays, &diagnostics);
    QList<PreparedScreenFrame> preparedFrames = preparedFramesFromRaw(rawFrames);
    markToneMappedDiagnostics(preparedFrames, &diagnostics);
    return SnapshotComposer::composeFrames(preparedFrames, diagnostics);
}

DesktopSnapshot DesktopCaptureService::composeFrames(const QList<CapturedScreenFrame>& frames) {
    const QList<PreparedScreenFrame> prepared = preparedFramesFromCaptured(frames);
    return SnapshotComposer::composeFrames(prepared);
}

QImage DesktopCaptureService::normalizeForSdr(const QImage& image) {
    return FrameNormalizer::normalizeToSdr(image);
}

QRect DesktopCaptureService::translateToVirtual(const QRect& localRect,
                                                const QPoint& virtualOrigin) {
    return SnapshotComposer::translateToVirtual(localRect, virtualOrigin);
}

QRect DesktopCaptureService::translateToVirtual(const DesktopSnapshot& snapshot,
                                                const QRect& localRect) {
    return SnapshotComposer::translateToVirtual(snapshot, localRect);
}

QPixmap DesktopCaptureService::copyLogicalSelection(const QPixmap& source,
                                                    const QRect& logicalRect) {
    return SnapshotComposer::copyLogicalSelection(source, logicalRect);
}

}  // namespace ais::capture
