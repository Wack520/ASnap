import unittest

from ai_screenshot.hotkeys import HotkeyParseError, parse_hotkey


class HotkeyParseTests(unittest.TestCase):
    def test_parse_alt_q(self):
        modifiers, vk = parse_hotkey('alt+q')
        self.assertEqual(vk, 0x51)
        self.assertNotEqual(modifiers, 0)

    def test_parse_ctrl_shift_1(self):
        modifiers, vk = parse_hotkey('ctrl+shift+1')
        self.assertEqual(vk, 0x31)
        self.assertNotEqual(modifiers, 0)

    def test_invalid_shortcut_raises(self):
        with self.assertRaises(HotkeyParseError):
            parse_hotkey('ctrl+alt')


if __name__ == '__main__':
    unittest.main()
