from __future__ import annotations

import ctypes
import sys
from dataclasses import dataclass

from PyQt6.QtCore import QPoint, QRect
from PyQt6.QtGui import QCursor, QGuiApplication, QPainter, QPixmap
from PIL import ImageGrab
from PIL.ImageQt import ImageQt

import win32api
import win32gui

from .placement import Rect


if sys.platform == "win32":
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    dwmapi = ctypes.windll.dwmapi
    GA_ROOT = 2
    GA_PARENT = 1
    GW_HWNDNEXT = 2
    DWMWA_EXTENDED_FRAME_BOUNDS = 9

    class RECT(ctypes.Structure):
        _fields_ = [
            ("left", wintypes.LONG),
            ("top", wintypes.LONG),
            ("right", wintypes.LONG),
            ("bottom", wintypes.LONG),
        ]

    class POINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]

    class HWNDPOINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


IGNORED_WINDOW_CLASSES = {
    "Progman",
    "WorkerW",
    "Shell_TrayWnd",
    "Shell_SecondaryTrayWnd",
}


@dataclass(slots=True)
class DesktopSnapshot:
    pixmap: QPixmap
    geometry: QRect


class ScreenService:
    def __init__(self):
        self._virtual_geometry_cache: QRect | None = None

    def get_virtual_geometry(self, refresh: bool = False) -> QRect:
        if not refresh and self._virtual_geometry_cache is not None:
            return QRect(self._virtual_geometry_cache)

        if sys.platform == "win32":
            monitors = win32api.EnumDisplayMonitors()
            if monitors:
                left = min(rect[0] for _handle, _dc, rect in monitors)
                top = min(rect[1] for _handle, _dc, rect in monitors)
                right = max(rect[2] for _handle, _dc, rect in monitors)
                bottom = max(rect[3] for _handle, _dc, rect in monitors)
                geometry = QRect(left, top, right - left, bottom - top)
                self._virtual_geometry_cache = QRect(geometry)
                return geometry

        screens = QGuiApplication.screens()
        if not screens:
            geometry = QRect(0, 0, 1920, 1080)
            self._virtual_geometry_cache = QRect(geometry)
            return geometry

        left = min(screen.geometry().left() for screen in screens)
        top = min(screen.geometry().top() for screen in screens)
        right = max(screen.geometry().right() for screen in screens)
        bottom = max(screen.geometry().bottom() for screen in screens)
        geometry = QRect(left, top, right - left + 1, bottom - top + 1)
        self._virtual_geometry_cache = QRect(geometry)
        return geometry

    def capture_virtual_desktop(self) -> DesktopSnapshot:
        geometry = self.get_virtual_geometry(refresh=True)
        try:
            image = ImageGrab.grab(all_screens=True)
            canvas = QPixmap.fromImage(ImageQt(image))
        except Exception:
            canvas = QPixmap(geometry.size())
            canvas.fill()
            painter = QPainter(canvas)
            for screen in QGuiApplication.screens():
                screen_geometry = screen.geometry()
                screen_pixmap = screen.grabWindow(0)
                target_point = screen_geometry.topLeft() - geometry.topLeft()
                painter.drawPixmap(target_point, screen_pixmap)
            painter.end()

        return DesktopSnapshot(pixmap=canvas, geometry=geometry)

    def locate_screen_for_rect(self, anchor: QRect) -> Rect:
        if sys.platform == "win32":
            center = anchor.center()
            best_rect = None
            best_area = -1
            for handle, _dc, monitor_rect in win32api.EnumDisplayMonitors():
                info = win32api.GetMonitorInfo(handle)
                work = info["Work"]
                work_rect = QRect(work[0], work[1], work[2] - work[0], work[3] - work[1])
                if work_rect.contains(center):
                    return self._to_rect(work_rect)
                intersection = work_rect.intersected(anchor)
                area = max(0, intersection.width()) * max(0, intersection.height())
                if area > best_area:
                    best_area = area
                    best_rect = work_rect
            if best_rect is not None:
                return self._to_rect(best_rect)

        screens = QGuiApplication.screens()
        if not screens:
            return Rect(0, 0, 1920, 1080)

        center = anchor.center()
        for screen in screens:
            available = screen.availableGeometry()
            if available.contains(center):
                return self._to_rect(available)

        best_geometry = screens[0].availableGeometry()
        best_area = -1
        for screen in screens:
            available = screen.availableGeometry()
            intersection = available.intersected(anchor)
            area = max(0, intersection.width()) * max(0, intersection.height())
            if area > best_area:
                best_area = area
                best_geometry = available
        return self._to_rect(best_geometry)

    def translate_to_virtual(self, local_rect: QRect, origin: QPoint) -> QRect:
        return QRect(local_rect.topLeft() + origin, local_rect.size())

    def detect_hover_window_rect(self, exclude_hwnd: int | None = None) -> QRect | None:
        candidates = self.candidate_rects_at_cursor(exclude_hwnd)
        return candidates[0] if candidates else None

    def candidate_rects_at_cursor(self, exclude_hwnd: int | None = None) -> list[QRect]:
        if sys.platform != "win32":
            return []

        point = native_cursor_pos()
        virtual_geometry = self.get_virtual_geometry()
        root_hwnd = top_level_window_from_point(point, exclude_hwnd=exclude_hwnd)
        if not root_hwnd:
            return []

        raw_rects: list[QRect] = []
        for hwnd in direct_candidate_chain(root_hwnd, point):
            rect = native_window_rect(hwnd)
            if rect is not None and rect.contains(point) and is_reasonable_candidate_rect(rect, virtual_geometry):
                raw_rects.append(rect)

        if not raw_rects:
            for hwnd in descendant_window_chain(root_hwnd, point):
                rect = native_window_rect(hwnd)
                if rect is not None and rect.contains(point) and is_reasonable_candidate_rect(rect, virtual_geometry):
                    raw_rects.append(rect)

            rect = native_window_rect(root_hwnd)
            if rect is not None and rect.contains(point) and is_reasonable_candidate_rect(rect, virtual_geometry):
                raw_rects.append(rect)

        return normalize_candidate_rects(raw_rects)

    def _to_rect(self, rect: QRect) -> Rect:
        return Rect(
            left=rect.left(),
            top=rect.top(),
            width=rect.width(),
            height=rect.height(),
        )


def is_ignorable_window_class(class_name: str) -> bool:
    return class_name in IGNORED_WINDOW_CLASSES


def is_reasonable_candidate_rect(rect: QRect, virtual_geometry: QRect) -> bool:
    if rect.width() < 60 or rect.height() < 40:
        return False
    if rect.width() >= virtual_geometry.width() - 4 and rect.height() >= virtual_geometry.height() - 4:
        return False
    return rect.intersects(virtual_geometry)


def native_cursor_pos() -> QPoint:
    if sys.platform != "win32":
        return QCursor.pos()
    point = POINT()
    if user32.GetCursorPos(ctypes.byref(point)):
        return QPoint(point.x, point.y)
    return QCursor.pos()


def window_class_name(hwnd: int) -> str:
    if sys.platform != "win32":
        return ""
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buffer, len(buffer))
    return buffer.value


def native_window_rect(hwnd: int) -> QRect | None:
    if sys.platform != "win32":
        return None
    rect = RECT()
    result = dwmapi.DwmGetWindowAttribute(
        hwnd,
        DWMWA_EXTENDED_FRAME_BOUNDS,
        ctypes.byref(rect),
        ctypes.sizeof(rect),
    )
    if result != 0:
        if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
            return None
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top)


def window_from_point(point: QPoint) -> int:
    if sys.platform != "win32":
        return 0
    native_point = POINT(point.x(), point.y())
    return int(user32.WindowFromPoint(native_point))


def top_level_window_from_point(point: QPoint, exclude_hwnd: int | None = None) -> int:
    if sys.platform != "win32":
        return 0
    hwnd = user32.GetTopWindow(0)
    while hwnd:
        if exclude_hwnd and int(hwnd) == int(exclude_hwnd):
            hwnd = user32.GetWindow(hwnd, GW_HWNDNEXT)
            continue
        if not user32.IsWindowVisible(hwnd) or user32.IsIconic(hwnd):
            hwnd = user32.GetWindow(hwnd, GW_HWNDNEXT)
            continue
        if is_ignorable_window_class(window_class_name(hwnd)):
            hwnd = user32.GetWindow(hwnd, GW_HWNDNEXT)
            continue
        rect = native_window_rect(hwnd)
        if rect is not None and rect.contains(point):
            return int(hwnd)
        hwnd = user32.GetWindow(hwnd, GW_HWNDNEXT)
    return 0


def descendant_window_chain(root_hwnd: int, screen_point: QPoint) -> list[int]:
    if sys.platform != "win32":
        return []
    chain: list[int] = []
    point_tuple = (screen_point.x(), screen_point.y())

    def callback(hwnd, _extra):
        try:
            if not win32gui.IsWindowVisible(hwnd):
                return True
            left, top, right, bottom = win32gui.GetWindowRect(hwnd)
            if not (left <= point_tuple[0] <= right and top <= point_tuple[1] <= bottom):
                return True
            class_name = win32gui.GetClassName(hwnd)
            if is_ignorable_window_class(class_name):
                return True
            chain.append(int(hwnd))
        except Exception:
            pass
        return True

    try:
        win32gui.EnumChildWindows(int(root_hwnd), callback, None)
    except Exception:
        pass

    chain.sort(key=lambda hwnd: _rect_area(native_window_rect(hwnd)))
    return chain


def direct_candidate_chain(root_hwnd: int, screen_point: QPoint) -> list[int]:
    if sys.platform != "win32":
        return []
    chain: list[int] = []
    seen: set[int] = set()

    child_hwnd = real_child_window_from_point(root_hwnd, screen_point)
    current = child_hwnd if child_hwnd and int(child_hwnd) != int(root_hwnd) else 0

    while current:
        current = int(current)
        if current in seen:
            break
        seen.add(current)
        chain.append(current)
        parent = parent_window(current)
        if not parent or int(parent) == int(current) or int(parent) == 0:
            break
        if int(parent) == int(root_hwnd):
            break
        current = int(parent)

    if int(root_hwnd) not in seen:
        chain.append(int(root_hwnd))
    return chain


def real_child_window_from_point(hwnd: int, screen_point: QPoint) -> int:
    if sys.platform != "win32":
        return 0
    client_point = POINT(screen_point.x(), screen_point.y())
    user32.ScreenToClient(hwnd, ctypes.byref(client_point))
    return int(user32.RealChildWindowFromPoint(hwnd, client_point))


def parent_window(hwnd: int) -> int:
    if sys.platform != "win32":
        return 0
    return int(user32.GetAncestor(hwnd, GA_PARENT))


def normalize_candidate_rects(rects: list[QRect]) -> list[QRect]:
    unique: dict[tuple[int, int, int, int], QRect] = {}
    for rect in rects:
        if rect.width() <= 0 or rect.height() <= 0:
            continue
        key = (rect.left(), rect.top(), rect.width(), rect.height())
        unique[key] = rect
    return sorted(unique.values(), key=lambda item: item.width() * item.height())


def _rect_area(rect: QRect | None) -> int:
    if rect is None:
        return 1 << 30
    return rect.width() * rect.height()
