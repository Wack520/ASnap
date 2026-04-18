from __future__ import annotations

from dataclasses import dataclass

from PyQt6.QtCore import QPoint, QRect, Qt, pyqtSignal
from PyQt6.QtGui import QColor, QPainter, QPen, QPixmap
from PyQt6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from .screen_service import DesktopSnapshot, ScreenService


ACCENT_COLOR = QColor("#1ea2fa")
ACCENT_FILL = QColor(30, 162, 250, 26)


@dataclass(slots=True)
class CaptureSelection:
    pixmap: QPixmap
    anchor_rect: QRect


class SnippingOverlay(QWidget):
    snippet_taken = pyqtSignal(object)
    capture_cancelled = pyqtSignal()

    def __init__(self, screen_service: ScreenService):
        super().__init__()
        self.screen_service = screen_service
        self.snapshot: DesktopSnapshot = self.screen_service.capture_virtual_desktop()
        self.setWindowFlags(
            Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.Tool
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setCursor(Qt.CursorShape.CrossCursor)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setGeometry(self.snapshot.geometry)

        self.start_point = QPoint()
        self.end_point = QPoint()
        self.is_drawing = False
        self.selection_made = False
        self.manual_rect = QRect()

    def showEvent(self, event) -> None:  # noqa: N802
        super().showEvent(event)
        self.raise_()
        self.activateWindow()
        self.setFocus(Qt.FocusReason.ActiveWindowFocusReason)
        self.grabMouse(Qt.CursorShape.CrossCursor)
        self.grabKeyboard()

    def paintEvent(self, event) -> None:  # noqa: N802
        painter = QPainter(self)
        clip_rect = event.rect()
        painter.drawPixmap(clip_rect, self.snapshot.pixmap, clip_rect)

        rect = self.current_rect()
        if rect.width() <= 0 or rect.height() <= 0:
            _draw_tag(
                painter,
                24,
                24,
                "画面已定格：直接拖拽框选区域，松开即可完成截图，Esc 退出",
            )
            return

        painter.setBrush(ACCENT_FILL)
        painter.setPen(QPen(ACCENT_COLOR, 2))
        painter.drawRect(rect.adjusted(0, 0, -1, -1))

        painter.setBrush(Qt.GlobalColor.white)
        painter.setPen(QPen(ACCENT_COLOR, 1))
        for point in _handles_for_rect(rect):
            painter.drawRect(point.x() - 3, point.y() - 3, 6, 6)

        _draw_tag(painter, rect.left(), max(4, rect.top() - 28), f"{rect.width()} × {rect.height()}")

        if not self.is_drawing:
            hint_y = rect.bottom() + 10
            if hint_y > self.height() - 28:
                hint_y = rect.bottom() - 26
            _draw_tag(painter, rect.right() - 220, hint_y, "松开完成截图 | Esc退出")

    def mousePressEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton:
            self.start_point = event.position().toPoint()
            self.end_point = self.start_point
            self.manual_rect = QRect()
            self.is_drawing = True
            self.update()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if self.is_drawing and event.buttons() & Qt.MouseButton.LeftButton:
            self.end_point = event.position().toPoint()
            self.update()

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton and self.is_drawing:
            self.is_drawing = False
            self.end_point = event.position().toPoint()
            self.manual_rect = QRect(self.start_point, self.end_point).normalized()
            self.update()
            self.confirm_selection()

    def mouseDoubleClickEvent(self, event) -> None:  # noqa: N802
        event.ignore()

    def keyPressEvent(self, event) -> None:  # noqa: N802
        if event.key() == Qt.Key.Key_Escape:
            self.close()
        elif event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self.confirm_selection()

    def wheelEvent(self, event) -> None:  # noqa: N802
        event.ignore()

    def confirm_selection(self) -> None:
        local_rect = self.current_rect()
        if local_rect.width() < 4 or local_rect.height() < 4:
            self.manual_rect = QRect()
            self.update()
            return

        anchor_rect = self.screen_service.translate_to_virtual(
            local_rect,
            self.snapshot.geometry.topLeft(),
        )
        selection = CaptureSelection(
            pixmap=self.snapshot.pixmap.copy(local_rect),
            anchor_rect=anchor_rect,
        )
        self.selection_made = True
        self.snippet_taken.emit(selection)
        self.close()

    def current_rect(self) -> QRect:
        if self.is_drawing:
            manual = QRect(self.start_point, self.end_point).normalized()
            if manual.width() > 0 and manual.height() > 0:
                return manual
        if self.manual_rect.width() > 0 and self.manual_rect.height() > 0:
            return self.manual_rect
        return QRect()

    def _global_to_local(self, rect: QRect) -> QRect:
        top_left = rect.topLeft() - self.snapshot.geometry.topLeft()
        return QRect(top_left, rect.size())

    def closeEvent(self, event) -> None:  # noqa: N802
        try:
            self.releaseMouse()
        except Exception:
            pass
        try:
            self.releaseKeyboard()
        except Exception:
            pass
        if not self.selection_made:
            self.capture_cancelled.emit()
        super().closeEvent(event)


class CropCanvas(QWidget):
    def __init__(self, source_pixmap: QPixmap):
        super().__init__()
        self.source_pixmap = source_pixmap
        self.selection_start = QPoint()
        self.selection_end = QPoint()
        self.is_drawing = False
        self.setMinimumSize(760, 420)
        self.setMouseTracking(True)

    def paintEvent(self, event) -> None:  # noqa: N802
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#111111"))

        image_rect = self.image_rect()
        if image_rect.width() <= 0 or image_rect.height() <= 0:
            return
        painter.drawPixmap(image_rect, self.source_pixmap)

        if self.current_selection().width() > 0 and self.current_selection().height() > 0:
            painter.setPen(QPen(ACCENT_COLOR, 2))
            painter.drawRect(self.current_selection())

        painter.setPen(QColor("#d0d7de"))
        painter.drawText(
            16,
            24,
            "拖拽选择二次裁剪区域；不框选则默认使用整张截图",
        )

    def image_rect(self) -> QRect:
        margin = 24
        available = self.rect().adjusted(margin, margin + 20, -margin, -margin)
        if self.source_pixmap.isNull() or available.width() <= 0 or available.height() <= 0:
            return QRect()

        scaled = self.source_pixmap.size()
        scaled.scale(available.size(), Qt.AspectRatioMode.KeepAspectRatio)
        x = available.left() + (available.width() - scaled.width()) // 2
        y = available.top() + (available.height() - scaled.height()) // 2
        return QRect(x, y, scaled.width(), scaled.height())

    def current_selection(self) -> QRect:
        rect = QRect(self.selection_start, self.selection_end).normalized()
        return rect.intersected(self.image_rect())

    def mousePressEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton and self.image_rect().contains(
            event.position().toPoint()
        ):
            self.selection_start = event.position().toPoint()
            self.selection_end = self.selection_start
            self.is_drawing = True
            self.update()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if self.is_drawing:
            self.selection_end = event.position().toPoint()
            self.update()

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton:
            self.is_drawing = False
            self.selection_end = event.position().toPoint()
            self.update()

    def get_cropped_pixmap(self) -> QPixmap:
        selection = self.current_selection()
        image_rect = self.image_rect()
        if selection.width() < 12 or selection.height() < 12:
            return self.source_pixmap

        scale_x = self.source_pixmap.width() / image_rect.width()
        scale_y = self.source_pixmap.height() / image_rect.height()
        mapped = QRect(
            int((selection.left() - image_rect.left()) * scale_x),
            int((selection.top() - image_rect.top()) * scale_y),
            max(1, int(selection.width() * scale_x)),
            max(1, int(selection.height() * scale_y)),
        )
        return self.source_pixmap.copy(mapped)


class CropEditorDialog(QDialog):
    def __init__(self, source_pixmap: QPixmap, parent=None):
        super().__init__(parent)
        self.setWindowTitle("二次裁剪")
        self.resize(980, 620)
        self.setWindowFlags(self.windowFlags() | Qt.WindowType.WindowStaysOnTopHint)

        self.canvas = CropCanvas(source_pixmap)
        title = QLabel("二次裁剪确认")
        title.setStyleSheet("font-size: 18px; font-weight: 600; color: #ffffff;")
        subtitle = QLabel("框选局部区域后发送给 AI；如果不框选，默认使用整张截图。")
        subtitle.setStyleSheet("color: #c0cad5;")

        use_full = QPushButton("使用整张")
        use_full.clicked.connect(self.accept)
        confirm = QPushButton("确认裁剪")
        confirm.clicked.connect(self.accept)
        cancel = QPushButton("取消")
        cancel.clicked.connect(self.reject)

        button_layout = QHBoxLayout()
        button_layout.addStretch()
        button_layout.addWidget(cancel)
        button_layout.addWidget(use_full)
        button_layout.addWidget(confirm)

        layout = QVBoxLayout()
        layout.addWidget(title)
        layout.addWidget(subtitle)
        layout.addWidget(self.canvas, 1)
        layout.addLayout(button_layout)
        self.setLayout(layout)

        self.setStyleSheet(
            """
            QDialog { background-color: #1f2329; }
            QPushButton {
                background-color: #2d333b;
                color: #ffffff;
                border: 1px solid #57606a;
                border-radius: 6px;
                padding: 6px 16px;
            }
            QPushButton:hover { background-color: #3a414a; }
            """
        )

    def get_cropped_pixmap(self) -> QPixmap:
        return self.canvas.get_cropped_pixmap()


def _draw_tag(painter: QPainter, x: int, y: int, text: str) -> None:
    metrics = painter.fontMetrics()
    box = metrics.boundingRect(text).adjusted(-6, -4, 6, 4)
    box.moveTo(x, y)
    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(QColor(0, 0, 0, 170))
    painter.drawRoundedRect(box, 4, 4)
    painter.setPen(QColor("#ffffff"))
    painter.drawText(box, Qt.AlignmentFlag.AlignCenter, text)


def _handles_for_rect(rect: QRect) -> list[QPoint]:
    return [
        rect.topLeft(),
        rect.topRight(),
        rect.bottomLeft(),
        rect.bottomRight(),
        QPoint(rect.center().x(), rect.top()),
        QPoint(rect.center().x(), rect.bottom()),
        QPoint(rect.left(), rect.center().y()),
        QPoint(rect.right(), rect.center().y()),
    ]


def _distance(point1: QPoint, point2: QPoint) -> int:
    delta = point1 - point2
    return abs(delta.x()) + abs(delta.y())
