#include "app/capture_workflow_controller.h"

#include <utility>

#include "capture/capture_overlay.h"
#include "capture/snapshot_composer.h"

namespace ais::app {

namespace {

[[nodiscard]] QString captureStatusText(
    const CaptureWorkflowController::LaunchMode mode) {
    return mode == CaptureWorkflowController::LaunchMode::AiAssistant
               ? QStringLiteral("Select an area to capture...")
               : QStringLiteral("选择截图区域…");
}

[[nodiscard]] bool isUsableSnapshot(const capture::DesktopSnapshot& snapshot) {
    if (snapshot.displayImage.isNull() || snapshot.captureImage.isNull()) {
        return false;
    }

    const QRect geometry = snapshot.overlayGeometry.isValid() ? snapshot.overlayGeometry
                                                              : snapshot.virtualGeometry;
    return geometry.isValid() && !geometry.isEmpty() &&
           snapshot.virtualGeometry.isValid() && !snapshot.virtualGeometry.isEmpty();
}

[[nodiscard]] QList<capture::DesktopSnapshot> overlaySnapshotsFor(
    const capture::DesktopSnapshot& snapshot) {
    QList<capture::DesktopSnapshot> overlaySnapshots;
    overlaySnapshots.reserve(snapshot.screenMappings.size());

    for (const capture::ScreenMapping& mapping : snapshot.screenMappings) {
        const capture::DesktopSnapshot perScreenSnapshot =
            capture::SnapshotComposer::snapshotForScreen(snapshot, mapping);
        if (!isUsableSnapshot(perScreenSnapshot)) {
            continue;
        }

        overlaySnapshots.append(perScreenSnapshot);
    }

    if (overlaySnapshots.isEmpty() && isUsableSnapshot(snapshot)) {
        overlaySnapshots.append(snapshot);
    }

    return overlaySnapshots;
}

}  // namespace

CaptureWorkflowController::CaptureWorkflowController(Hooks hooks, QObject* parent)
    : QObject(parent),
      hooks_(std::move(hooks)) {}

CaptureWorkflowController::~CaptureWorkflowController() {
    clear();
}

bool CaptureWorkflowController::start(const LaunchMode mode) {
    clear();
    launchMode_ = mode;

    if (hooks_.syncStatus) {
        hooks_.syncStatus(captureStatusText(launchMode_));
    }

    if (!hooks_.captureDesktop) {
        if (hooks_.syncStatus) {
            hooks_.syncStatus(QStringLiteral("Capture service is unavailable"));
        }
        return false;
    }

    const capture::DesktopSnapshot snapshot = hooks_.captureDesktop();
    if (!isUsableSnapshot(snapshot)) {
        if (hooks_.syncStatus) {
            hooks_.syncStatus(QStringLiteral("Failed to capture desktop"));
        }
        return false;
    }

    const QList<capture::DesktopSnapshot> overlaySnapshots = overlaySnapshotsFor(snapshot);
    for (const capture::DesktopSnapshot& overlaySnapshot : overlaySnapshots) {
        auto* overlay = new capture::CaptureOverlay(overlaySnapshot);
        connect(overlay, &capture::CaptureOverlay::captureConfirmed,
                this, &CaptureWorkflowController::onCaptureConfirmed);
        connect(overlay, &capture::CaptureOverlay::captureCancelled,
                this, &CaptureWorkflowController::onCaptureCancelled);
        overlays_.append(overlay);
    }

    if (overlays_.isEmpty()) {
        if (hooks_.syncStatus) {
            hooks_.syncStatus(QStringLiteral("Failed to capture desktop"));
        }
        return false;
    }

    for (capture::CaptureOverlay* overlay : std::as_const(overlays_)) {
        if (overlay == nullptr) {
            continue;
        }

        overlay->show();
        overlay->raise();
        overlay->activateWindow();
    }

    return true;
}

void CaptureWorkflowController::clear() {
    const QList<capture::CaptureOverlay*> overlays = std::exchange(overlays_, {});
    for (capture::CaptureOverlay* overlay : overlays) {
        if (overlay == nullptr) {
            continue;
        }

        overlay->hide();
        overlay->deleteLater();
    }
}

void CaptureWorkflowController::onCaptureConfirmed(
    const capture::CaptureSelection& selection) {
    clear();
    if (hooks_.onConfirmed) {
        hooks_.onConfirmed(selection);
    }
}

void CaptureWorkflowController::onCaptureCancelled() {
    clear();
    if (hooks_.onCancelled) {
        hooks_.onCancelled();
    }
    if (hooks_.syncStatus) {
        hooks_.syncStatus(QStringLiteral("Capture cancelled"));
    }
}

}  // namespace ais::app
