import unittest
from unittest.mock import patch

from PyQt6.QtCore import QPoint, QRect
from ai_screenshot.screen_service import direct_candidate_chain, normalize_candidate_rects


class CandidateHierarchyTests(unittest.TestCase):
    def test_normalize_candidate_rects_orders_small_to_large_and_dedupes(self):
        rects = [
            QRect(100, 100, 800, 600),
            QRect(120, 130, 500, 300),
            QRect(120, 130, 500, 300),
            QRect(130, 140, 200, 100),
        ]

        normalized = normalize_candidate_rects(rects)

        self.assertEqual(len(normalized), 3)
        self.assertEqual(normalized[0], QRect(130, 140, 200, 100))
        self.assertEqual(normalized[-1], QRect(100, 100, 800, 600))

    def test_direct_candidate_chain_prefers_small_child_then_parents(self):
        with patch(
            'ai_screenshot.screen_service.real_child_window_from_point',
            return_value=30,
        ), patch(
            'ai_screenshot.screen_service.parent_window',
            side_effect=lambda hwnd: {30: 20, 20: 100, 100: 0}.get(hwnd, 0),
        ):
            chain = direct_candidate_chain(100, QPoint(20, 20))

        self.assertEqual(chain, [30, 20, 100])


if __name__ == '__main__':
    unittest.main()
