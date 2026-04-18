import unittest

from ai_screenshot.placement import Rect, Size, choose_panel_position


class PlacementTests(unittest.TestCase):
    def test_prefers_nearest_available_slot(self):
        anchor = Rect(left=100, top=100, width=200, height=120)
        screen = Rect(left=0, top=0, width=900, height=700)

        point = choose_panel_position(anchor, Size(width=280, height=180), screen, margin=16)

        self.assertEqual(point, (316, 116))

    def test_falls_back_inside_screen_when_right_side_overflows(self):
        anchor = Rect(left=740, top=500, width=120, height=100)
        screen = Rect(left=0, top=0, width=900, height=700)

        point = choose_panel_position(anchor, Size(width=260, height=180), screen, margin=16)

        self.assertGreaterEqual(point[0], 0)
        self.assertGreaterEqual(point[1], 0)
        self.assertLessEqual(point[0] + 260, 900)
        self.assertLessEqual(point[1] + 180, 700)


if __name__ == '__main__':
    unittest.main()
