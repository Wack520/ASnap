#pragma once

#include <functional>

#include <QList>
#include <QObject>
#include <QString>

#include "capture/desktop_snapshot.h"

namespace ais::capture {
class CaptureOverlay;
struct CaptureSelection;
}  // namespace ais::capture

namespace ais::app {

class CaptureWorkflowController final : public QObject {
    Q_OBJECT

public:
    enum class LaunchMode {
        AiAssistant,
        PlainScreenshot,
    };

    struct Hooks {
        std::function<ais::capture::DesktopSnapshot()> captureDesktop;
        std::function<void(const ais::capture::CaptureSelection&)> onConfirmed;
        std::function<void()> onCancelled;
        std::function<void(const QString&)> syncStatus;
    };

    explicit CaptureWorkflowController(Hooks hooks, QObject* parent = nullptr);
    ~CaptureWorkflowController() override;

    [[nodiscard]] bool start(LaunchMode mode);
    void clear();

private:
    void onCaptureConfirmed(const ais::capture::CaptureSelection& selection);
    void onCaptureCancelled();

    Hooks hooks_;
    QList<capture::CaptureOverlay*> overlays_;
    LaunchMode launchMode_ = LaunchMode::AiAssistant;
};

}  // namespace ais::app
