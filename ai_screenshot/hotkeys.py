from __future__ import annotations

import ctypes
from ctypes import wintypes

from PyQt6.QtCore import QObject, Qt, pyqtSignal
from PyQt6.QtWidgets import QWidget


user32 = ctypes.windll.user32

WM_HOTKEY = 0x0312
MOD_ALT = 0x0001
MOD_CONTROL = 0x0002
MOD_SHIFT = 0x0004
MOD_WIN = 0x0008
MOD_NOREPEAT = 0x4000


class MSG(ctypes.Structure):
    _fields_ = [
        ("hwnd", wintypes.HWND),
        ("message", wintypes.UINT),
        ("wParam", wintypes.WPARAM),
        ("lParam", wintypes.LPARAM),
        ("time", wintypes.DWORD),
        ("pt", wintypes.POINT),
        ("lPrivate", wintypes.DWORD),
    ]


class HotkeyParseError(ValueError):
    """Raised when a global hotkey cannot be parsed."""


MODIFIER_MAP = {
    "alt": MOD_ALT,
    "ctrl": MOD_CONTROL,
    "control": MOD_CONTROL,
    "shift": MOD_SHIFT,
    "win": MOD_WIN,
    "meta": MOD_WIN,
}

SPECIAL_KEY_MAP = {
    "space": 0x20,
    "tab": 0x09,
    "enter": 0x0D,
    "esc": 0x1B,
    "escape": 0x1B,
}


def parse_hotkey(shortcut: str) -> tuple[int, int]:
    tokens = [token.strip().lower() for token in shortcut.split("+") if token.strip()]
    if not tokens:
        raise HotkeyParseError("快捷键不能为空。")

    modifiers = 0
    key_token = None
    for token in tokens:
        if token in MODIFIER_MAP:
            modifiers |= MODIFIER_MAP[token]
            continue
        if key_token is not None:
            raise HotkeyParseError(f"快捷键里有多个主键：{shortcut}")
        key_token = token

    if key_token is None:
        raise HotkeyParseError("快捷键缺少主键。")

    if key_token in SPECIAL_KEY_MAP:
        return modifiers | MOD_NOREPEAT, SPECIAL_KEY_MAP[key_token]

    if len(key_token) == 1:
        char = key_token.upper()
        if "A" <= char <= "Z" or "0" <= char <= "9":
            return modifiers | MOD_NOREPEAT, ord(char)

    if key_token.startswith("f") and key_token[1:].isdigit():
        fn = int(key_token[1:])
        if 1 <= fn <= 24:
            return modifiers | MOD_NOREPEAT, 0x6F + fn

    raise HotkeyParseError(f"不支持的快捷键写法：{shortcut}")


class HotkeyMessageWindow(QWidget):
    triggered = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.setWindowFlags(
            Qt.WindowType.Tool
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowDoesNotAcceptFocus
        )
        self.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
        self._registered_id: int | None = None

    def nativeEvent(self, eventType, message):  # noqa: N802
        if eventType == b"windows_generic_MSG":
            msg = MSG.from_address(int(message))
            if msg.message == WM_HOTKEY and self._registered_id is not None:
                if int(msg.wParam) == self._registered_id:
                    self.triggered.emit()
                    return True, 0
        return False, 0


class NativeHotkeyManager(QObject):
    triggered = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.window = HotkeyMessageWindow()
        self.window.triggered.connect(self.triggered.emit)
        self.window.show()
        self.hotkey_id = 1
        self.shortcut = ""

    def register(self, shortcut: str) -> None:
        self.unregister()
        modifiers, vk = parse_hotkey(shortcut)
        if not user32.RegisterHotKey(int(self.window.winId()), self.hotkey_id, modifiers, vk):
            raise OSError(f"注册全局快捷键失败：{shortcut}")
        self.shortcut = shortcut
        self.window._registered_id = self.hotkey_id

    def update_shortcut(self, shortcut: str) -> None:
        self.register(shortcut)

    def unregister(self) -> None:
        user32.UnregisterHotKey(int(self.window.winId()), self.hotkey_id)
        self.window._registered_id = None
