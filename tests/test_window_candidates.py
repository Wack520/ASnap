import unittest

from PyQt6.QtCore import QRect
from ai_screenshot.screen_service import is_ignorable_window_class, is_reasonable_candidate_rect


class WindowCandidateTests(unittest.TestCase):
    def test_ignorable_shell_window_classes(self):
        self.assertTrue(is_ignorable_window_class('Progman'))
        self.assertTrue(is_ignorable_window_class('WorkerW'))
        self.assertTrue(is_ignorable_window_class('Shell_TrayWnd'))
        self.assertFalse(is_ignorable_window_class('Chrome_WidgetWin_1'))

    def test_reasonable_candidate_rect_rejects_desktop_sized_rect(self):
        virtual = QRect(0, 0, 3840, 1080)
        self.assertFalse(is_reasonable_candidate_rect(QRect(0, 0, 3840, 1080), virtual))
        self.assertFalse(is_reasonable_candidate_rect(QRect(0, 0, 30, 20), virtual))
        self.assertTrue(is_reasonable_candidate_rect(QRect(100, 100, 800, 600), virtual))


if __name__ == '__main__':
    unittest.main()
