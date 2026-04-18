from __future__ import annotations

import ctypes
import sys

from .app import create_application


def enable_windows_dpi_awareness() -> None:
    if sys.platform != "win32":
        return
    try:
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ctypes.c_void_p(-4)
        ctypes.windll.user32.SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        )
    except Exception:
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except Exception:
            pass


def main() -> int:
    enable_windows_dpi_awareness()
    app, _controller = create_application(start_controller=True)
    return app.exec()
