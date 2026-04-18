from __future__ import annotations

import sys

from PyQt6.QtCore import QByteArray, QBuffer, QIODevice, QObject, QTimer, Qt
from PyQt6.QtGui import QAction, QColor, QIcon, QPainter, QPen, QPixmap
from PyQt6.QtWidgets import QApplication, QMenu, QSystemTrayIcon

from .config import AppConfig
from .hotkeys import HotkeyParseError, NativeHotkeyManager
from .placement import Rect as PlacementRect
from .placement import Size, choose_panel_position
from .providers import ProviderRegistry
from .screen_service import ScreenService
from .session import ChatSession
from .ui_capture import CaptureSelection, SnippingOverlay
from .ui_chat import FloatingChatPanel, SettingsDialog


DEFAULT_ANALYSIS_PROMPT = (
    "你将收到一张用户刚刚框选的屏幕截图。"
    "请只基于截图中真实可见的内容做简明分析，"
    "不要把截图工具本身的边框、尺寸标签、遮罩提示当成目标内容。"
    "如果截图区域明显截错、过暗、几乎空白或不是用户想看的主体，请直接说明“截图区域不对，请重新截取”。"
    "然后再指出关键问题、异常或下一步建议。"
)


class AppController(QObject):
    def __init__(self):
        super().__init__()
        self.registry = ProviderRegistry()
        self.screen_service = ScreenService()
        self.config = AppConfig.load()

        self.overlay: SnippingOverlay | None = None
        self.chat_panel: FloatingChatPanel | None = None
        self.current_anchor_rect = None
        self.current_pixmap = QPixmap()

        self.tray = QSystemTrayIcon()
        self.tray.setIcon(self._build_tray_icon())
        self.tray.setToolTip(
            f"AI 截图助手\n后台运行中，可按 {self.config.shortcut} 进行截图"
        )
        self.tray.setContextMenu(self._build_menu())
        self.tray.show()

        self.hotkey_manager = NativeHotkeyManager()
        self.hotkey_manager.triggered.connect(self.trigger_capture)
        self._register_hotkey(self.config.shortcut)

        QApplication.instance().aboutToQuit.connect(self.shutdown)

        active_profile = self.config.get_active_profile()
        if not active_profile.base_url or not active_profile.model_name:
            self.show_settings()
        else:
            self.tray.showMessage(
                "AI 截图助手已就绪",
                f"按 {self.config.shortcut} 可开始截图并继续追问 AI。",
                QSystemTrayIcon.MessageIcon.Information,
            )

    def _build_menu(self) -> QMenu:
        menu = QMenu()
        menu.setStyleSheet(
            """
            QMenu { background-color: #1f2329; color: #d0d7de; border: 1px solid #444c56; }
            QMenu::item { padding: 6px 22px; }
            QMenu::item:selected { background-color: #2d74da; color: white; }
            """
        )
        action_capture = QAction("开始截图", self)
        action_capture.triggered.connect(self.trigger_capture)
        action_settings = QAction("设置", self)
        action_settings.triggered.connect(self.show_settings)
        action_quit = QAction("退出", self)
        action_quit.triggered.connect(QApplication.instance().quit)
        menu.addAction(action_capture)
        menu.addAction(action_settings)
        menu.addSeparator()
        menu.addAction(action_quit)
        return menu

    def _build_tray_icon(self) -> QIcon:
        pixmap = QPixmap(32, 32)
        pixmap.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setBrush(QColor("#2d74da"))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawRoundedRect(2, 2, 28, 28, 8, 8)
        painter.setPen(QPen(QColor("#ffffff"), 2))
        painter.drawText(pixmap.rect(), Qt.AlignmentFlag.AlignCenter, "AI")
        painter.end()
        return QIcon(pixmap)

    def show_settings(self) -> None:
        dialog = SettingsDialog(self.config, self.registry)
        if dialog.exec():
            new_config = dialog.get_config()
            if new_config.shortcut != self.config.shortcut:
                self._register_hotkey(new_config.shortcut)
            self.config = new_config
            self.config.save()
            self.tray.setToolTip(
                f"AI 截图助手\n后台运行中，可按 {self.config.shortcut} 进行截图"
            )
            if self.chat_panel:
                self.chat_panel.apply_theme(self.config.theme, self.config.opacity)

    def trigger_capture(self) -> None:
        QTimer.singleShot(100, self._start_capture)

    def _start_capture(self) -> None:
        active_profile = self.config.get_active_profile()
        if not active_profile.base_url or not active_profile.model_name:
            self.tray.showMessage(
                "配置不完整",
                "请先在设置里填写 Base URL 和模型名称。",
                QSystemTrayIcon.MessageIcon.Warning,
            )
            self.show_settings()
            return

        if self.overlay and self.overlay.isVisible():
            self.overlay.close()
            self.overlay.deleteLater()
            self.overlay = None
            QApplication.processEvents()

        if self.chat_panel and self.chat_panel.isVisible():
            self.chat_panel.hide()
            QApplication.processEvents()

        self.overlay = SnippingOverlay(self.screen_service)
        self.overlay.snippet_taken.connect(self.on_selection_captured)
        self.overlay.capture_cancelled.connect(self.on_capture_cancelled)
        self.overlay.show()
        self.overlay.activateWindow()

    def on_selection_captured(self, selection: CaptureSelection) -> None:
        self.overlay = None
        self.current_anchor_rect = selection.anchor_rect
        self.current_pixmap = selection.pixmap
        self._open_chat_panel(selection.pixmap, selection.anchor_rect)

    def on_capture_cancelled(self) -> None:
        self.overlay = None
        if self.chat_panel:
            self.chat_panel.show()

    def _open_chat_panel(self, pixmap: QPixmap, anchor_rect) -> None:
        session = ChatSession(default_prompt=DEFAULT_ANALYSIS_PROMPT)
        image_bytes = pixmap_to_png_bytes(pixmap)
        session.set_image_bytes(image_bytes)
        session.start_with_default_prompt()

        profile = self.config.get_active_profile()
        if self.chat_panel:
            self.chat_panel.close()

        self.chat_panel = FloatingChatPanel(self.registry, session, profile)
        self.chat_panel.recrop_requested.connect(self.handle_recrop_request)
        self.chat_panel.set_context(pixmap, session, profile)
        self.chat_panel.apply_theme(self.config.theme, self.config.opacity)
        self.chat_panel.show()
        self._position_chat_panel(anchor_rect)
        self.chat_panel.start_default_analysis()

    def handle_recrop_request(self) -> None:
        self.trigger_capture()

    def _position_chat_panel(self, anchor_rect) -> None:
        if self.chat_panel is None:
            return
        screen_rect = self.screen_service.locate_screen_for_rect(anchor_rect)
        anchor = PlacementRect(
            left=anchor_rect.left(),
            top=anchor_rect.top(),
            width=anchor_rect.width(),
            height=anchor_rect.height(),
        )
        panel_size = Size(self.chat_panel.width(), self.chat_panel.height())
        x, y = choose_panel_position(anchor, panel_size, screen_rect)
        self.chat_panel.move(x, y)

    def shutdown(self) -> None:
        self.hotkey_manager.unregister()

    def _register_hotkey(self, shortcut: str) -> None:
        try:
            self.hotkey_manager.update_shortcut(shortcut)
        except HotkeyParseError as exc:
            self.tray.showMessage(
                "快捷键格式错误",
                str(exc),
                QSystemTrayIcon.MessageIcon.Warning,
            )
        except Exception as exc:
            self.tray.showMessage(
                "快捷键注册失败",
                f"{exc}\n建议换成 Ctrl+Shift+A 之类的组合键。",
                QSystemTrayIcon.MessageIcon.Warning,
            )


def pixmap_to_png_bytes(pixmap: QPixmap) -> bytes:
    byte_array = QByteArray()
    buffer = QBuffer(byte_array)
    buffer.open(QIODevice.OpenModeFlag.WriteOnly)
    pixmap.save(buffer, "PNG")
    return bytes(byte_array.data())


def create_application(start_controller: bool = True):
    app = QApplication.instance()
    if app is None:
        app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    controller = AppController() if start_controller else None
    return app, controller
