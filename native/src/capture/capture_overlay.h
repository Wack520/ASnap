#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

#include "capture/capture_selection.h"
#include "capture/desktop_snapshot.h"

class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

namespace ais::capture {

class CaptureOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit CaptureOverlay(DesktopSnapshot snapshot, QWidget* parent = nullptr);

signals:
    void captureConfirmed(const ais::capture::CaptureSelection& selection);
    void captureCancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    [[nodiscard]] QRect currentLocalSelection() const;
    [[nodiscard]] QPoint clampToBounds(const QPoint& point) const;
    [[nodiscard]] QRect localScreenRectAt(const QPoint& point) const;
    void confirmSelection();
    void confirmSelection(const QRect& localSelection);

    DesktopSnapshot snapshot_;
    QPoint dragStart_;
    QPoint dragCurrent_;
    QRect finishedSelection_;
    bool dragging_ = false;
    bool confirmed_ = false;
};

}  // namespace ais::capture
