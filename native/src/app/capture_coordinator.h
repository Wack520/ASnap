#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QString>

#include "app/app_busy_state.h"
#include "capture/desktop_snapshot.h"

namespace ais::capture {
struct CaptureSelection;
}  // namespace ais::capture

namespace ais::app {

class CaptureWorkflowController;

class CaptureCoordinator final : public QObject {
    Q_OBJECT

public:
    struct Hooks {
        std::function<capture::DesktopSnapshot()> captureDesktop;
        std::function<void(const capture::CaptureSelection&)> onAiSelection;
        std::function<void(const capture::CaptureSelection&)> onPlainSelection;
        std::function<void()> onCancelled;
        std::function<void(const QString&)> syncStatus;
    };

    explicit CaptureCoordinator(Hooks hooks = {}, QObject* parent = nullptr);
    ~CaptureCoordinator() override;

    [[nodiscard]] bool canStartCaptureForState(BusyState state) const noexcept;
    [[nodiscard]] bool shouldCancelConversationForState(BusyState state) const noexcept;
    [[nodiscard]] bool startAiCapture();
    [[nodiscard]] bool startPlainCapture();
    void confirmCaptureForTest(const capture::CaptureSelection& selection, bool sendToAi);

private:
    [[nodiscard]] bool startCaptureWorkflow(bool sendToAi);

    Hooks hooks_;
    std::unique_ptr<CaptureWorkflowController> workflowController_;
};

}  // namespace ais::app
