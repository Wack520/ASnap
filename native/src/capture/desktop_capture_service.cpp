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

void markToneMappedDiagnostic(const PreparedScreenFrame& preparedFrame,
                              CaptureDiagnostics* diagnostics) {
    if (diagnostics == nullptr || diagnostics->entries.isEmpty() || !preparedFrame.hdrToneMapped) {
        return;
    }

    for (CaptureDiagnosticsEntry& entry : diagnostics->entries) {
        if (normalizedScreenName(entry.deviceName) ==
            normalizedScreenName(preparedFrame.display.deviceName)) {
            entry.hdrToneMapped = true;
            break;
        }
    }
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

    const QList<DisplayDescriptor> displays = topology_->enumerateDisplays();
    CaptureDiagnostics diagnostics;
    const QList<RawScreenFrame> rawFrames = backend_->captureDisplays(displays, &diagnostics);

    QList<PreparedScreenFrame> preparedFrames;
    preparedFrames.reserve(rawFrames.size());
    for (const RawScreenFrame& frame : rawFrames) {
        const PreparedScreenFrame preparedFrame = FrameNormalizer::normalizeFrame(frame);
        markToneMappedDiagnostic(preparedFrame, &diagnostics);
        preparedFrames.append(preparedFrame);
    }

    return SnapshotComposer::composeFrames(preparedFrames, diagnostics);
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
