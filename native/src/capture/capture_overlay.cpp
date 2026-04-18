#include "capture/capture_overlay.h"

#include <algorithm>

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include "capture/desktop_capture_service.h"

namespace ais::capture {

namespace {

constexpr int kMinimumSelectionExtent = 4;
constexpr int kCornerLength = 18;
const QColor kSelectionFill(0, 0, 0, 0);
const QColor kSelectionOutline(255, 255, 255, 180);
const QColor kShadeColor(0, 0, 0, 96);

void shadeOutsideSelection(QPainter* painter, const QRect& bounds, const QRect& selection) {
    if (painter == nullptr || !selection.isValid() || selection.isEmpty()) {
        return;
    }

    const int leftWidth = qMax(0, selection.left() - bounds.left());
    const int rightX = selection.right() + 1;
    const int rightWidth = qMax(0, bounds.right() - selection.right());
    const int topHeight = qMax(0, selection.top() - bounds.top());
    const int bottomY = selection.bottom() + 1;
    const int bottomHeight = qMax(0, bounds.bottom() - selection.bottom());

    if (topHeight > 0) {
        painter->fillRect(QRect(bounds.left(), bounds.top(), bounds.width(), topHeight), kShadeColor);
    }
    if (bottomHeight > 0) {
        painter->fillRect(QRect(bounds.left(), bottomY, bounds.width(), bottomHeight), kShadeColor);
    }
    if (leftWidth > 0) {
        painter->fillRect(QRect(bounds.left(), selection.top(), leftWidth, selection.height()), kShadeColor);
    }
    if (rightWidth > 0) {
        painter->fillRect(QRect(rightX, selection.top(), rightWidth, selection.height()), kShadeColor);
    }
}

void drawSelectionCorners(QPainter* painter, const QRect& selection) {
    if (painter == nullptr || !selection.isValid() || selection.isEmpty()) {
        return;
    }

    const QRect borderRect = selection.adjusted(0, 0, -1, -1);
    if (borderRect.width() < 2 || borderRect.height() < 2) {
        return;
    }

    const int cornerExtent = std::min(
        {kCornerLength,
         qMax(2, (borderRect.width() + 1) / 2),
         qMax(2, (borderRect.height() + 1) / 2)});

    painter->drawLine(borderRect.topLeft(),
                      QPoint(borderRect.left() + cornerExtent, borderRect.top()));
    painter->drawLine(borderRect.topLeft(),
                      QPoint(borderRect.left(), borderRect.top() + cornerExtent));

    painter->drawLine(borderRect.topRight(),
                      QPoint(borderRect.right() - cornerExtent, borderRect.top()));
    painter->drawLine(borderRect.topRight(),
                      QPoint(borderRect.right(), borderRect.top() + cornerExtent));

    painter->drawLine(borderRect.bottomLeft(),
                      QPoint(borderRect.left() + cornerExtent, borderRect.bottom()));
    painter->drawLine(borderRect.bottomLeft(),
                      QPoint(borderRect.left(), borderRect.bottom() - cornerExtent));

    painter->drawLine(borderRect.bottomRight(),
                      QPoint(borderRect.right() - cornerExtent, borderRect.bottom()));
    painter->drawLine(borderRect.bottomRight(),
                      QPoint(borderRect.right(), borderRect.bottom() - cornerExtent));
}

}  // namespace

CaptureOverlay::CaptureOverlay(DesktopSnapshot snapshot, QWidget* parent)
    : QWidget(parent),
      snapshot_(std::move(snapshot)) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setGeometry(snapshot_.virtualGeometry);
}

void CaptureOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.drawPixmap(QPoint(0, 0), snapshot_.displayImage);

    const QRect selection = currentLocalSelection();
    if (!selection.isValid() || selection.isEmpty()) {
        return;
    }

    shadeOutsideSelection(&painter, rect(), selection);
    painter.setPen(QPen(kSelectionOutline, 2));
    painter.setBrush(kSelectionFill);
    drawSelectionCorners(&painter, selection);
}

void CaptureOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    dragStart_ = clampToBounds(event->position().toPoint());
    dragCurrent_ = dragStart_;
    finishedSelection_ = QRect();
    dragging_ = true;
    update();
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_ || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    dragCurrent_ = clampToBounds(event->position().toPoint());
    update();
}

void CaptureOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !dragging_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    dragCurrent_ = clampToBounds(event->position().toPoint());
    finishedSelection_ = QRect(dragStart_, dragCurrent_).normalized();
    dragging_ = false;
    update();
    confirmSelection();
}

void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    if (!currentLocalSelection().isValid() || currentLocalSelection().isEmpty()) {
        dragging_ = false;
        finishedSelection_ = rect().adjusted(0, 0, -1, -1);
        update();
        confirmSelection(finishedSelection_);
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void CaptureOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        confirmSelection();
        return;
    }

    QWidget::keyPressEvent(event);
}

void CaptureOverlay::closeEvent(QCloseEvent* event) {
    if (!confirmed_) {
        emit captureCancelled();
    }
    QWidget::closeEvent(event);
}

QRect CaptureOverlay::currentLocalSelection() const {
    if (dragging_) {
        return QRect(dragStart_, dragCurrent_).normalized();
    }
    return finishedSelection_;
}

QPoint CaptureOverlay::clampToBounds(const QPoint& point) const {
    const QRect bounds = rect().adjusted(0, 0, -1, -1);
    return QPoint(
        std::clamp(point.x(), bounds.left(), bounds.right()),
        std::clamp(point.y(), bounds.top(), bounds.bottom()));
}

void CaptureOverlay::confirmSelection() {
    confirmSelection(currentLocalSelection());
}

void CaptureOverlay::confirmSelection(const QRect& localSelection) {
    if (!localSelection.isValid() || localSelection.width() < kMinimumSelectionExtent ||
        localSelection.height() < kMinimumSelectionExtent) {
        finishedSelection_ = QRect();
        update();
        return;
    }

    confirmed_ = true;
    const QPixmap captureSource = snapshot_.captureImage.isNull() ? snapshot_.displayImage : snapshot_.captureImage;
    emit captureConfirmed(CaptureSelection{
        .image = DesktopCaptureService::copyLogicalSelection(captureSource, localSelection),
        .localRect = localSelection,
        .virtualRect = DesktopCaptureService::translateToVirtual(
            localSelection,
            snapshot_.virtualGeometry.topLeft()),
    });
    close();
}

}  // namespace ais::capture
