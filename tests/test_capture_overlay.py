import os
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PyQt6.QtCore import QRect
from PyQt6.QtGui import QColor, QPixmap
from PyQt6.QtWidgets import QApplication

from ai_screenshot.screen_service import DesktopSnapshot
from ai_screenshot.ui_capture import SnippingOverlay


class _FakeScreenService:
    def capture_virtual_desktop(self):
        pixmap = QPixmap(300, 200)
        pixmap.fill(QColor("#101010"))
        return DesktopSnapshot(pixmap=pixmap, geometry=QRect(0, 0, 300, 200))

    def candidate_rects_at_cursor(self, exclude_hwnd=None):
        return [QRect(20, 30, 120, 80)]

    def translate_to_virtual(self, local_rect, origin):
        return QRect(local_rect.topLeft() + origin, local_rect.size())


class CaptureOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def test_overlay_uses_manual_selection_mode(self):
        overlay = SnippingOverlay(_FakeScreenService())
        self.assertFalse(overlay.hasMouseTracking())

        overlay.show()
        self.app.processEvents()

        self.assertEqual(overlay.current_rect(), QRect())
        overlay.close()


if __name__ == "__main__":
    unittest.main()
