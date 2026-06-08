#include "app/capture_coordinator.h"

#include <utility>

#include "app/capture_workflow_controller.h"
#include "capture/capture_selection.h"

namespace ais::app {

CaptureCoordinator::CaptureCoordinator(Hooks hooks, QObject* parent)
    : QObject(parent),
      hooks_(std::move(hooks)) {}

CaptureCoordinator::~CaptureCoordinator() = default;

bool CaptureCoordinator::canStartCaptureForState(const BusyState state) const noexcept {
    return state != BusyState::Capturing && state != BusyState::TestingProvider;
}

bool CaptureCoordinator::shouldCancelConversationForState(const BusyState state) const noexcept {
    return state == BusyState::RequestInFlight;
}

bool CaptureCoordinator::startAiCapture() {
    return startCaptureWorkflow(true);
}

bool CaptureCoordinator::startPlainCapture() {
    return startCaptureWorkflow(false);
}

void CaptureCoordinator::confirmCaptureForTest(const capture::CaptureSelection& selection,
                                               const bool sendToAi) {
    if (sendToAi) {
        if (hooks_.onAiSelection != nullptr) {
            hooks_.onAiSelection(selection);
        }
        return;
    }

    if (hooks_.onPlainSelection != nullptr) {
        hooks_.onPlainSelection(selection);
    }
}

bool CaptureCoordinator::startCaptureWorkflow(const bool sendToAi) {
    CaptureWorkflowController::Hooks workflowHooks;
    workflowHooks.captureDesktop = hooks_.captureDesktop;
    workflowHooks.onConfirmed =
        [this, sendToAi](const capture::CaptureSelection& selection) {
            if (sendToAi) {
                if (hooks_.onAiSelection != nullptr) {
                    hooks_.onAiSelection(selection);
                }
                return;
            }

            if (hooks_.onPlainSelection != nullptr) {
                hooks_.onPlainSelection(selection);
            }
        };
    workflowHooks.onCancelled = hooks_.onCancelled;
    workflowHooks.syncStatus = hooks_.syncStatus;

    workflowController_ = std::make_unique<CaptureWorkflowController>(std::move(workflowHooks), this);
    return workflowController_->start(
        sendToAi ? CaptureWorkflowController::LaunchMode::AiAssistant
                 : CaptureWorkflowController::LaunchMode::PlainScreenshot);
}

}  // namespace ais::app
