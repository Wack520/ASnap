from __future__ import annotations

import html

import markdown
from PyQt6.QtCore import QThread, Qt, pyqtSignal
from PyQt6.QtGui import QPixmap
from PyQt6.QtWidgets import (
    QComboBox,
    QDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from .config import AppConfig, ProviderProfile
from .provider_presets import PRESETS, ProviderPreset, get_provider_preset
from .providers import ProviderRegistry
from .session import ChatSession


PROVIDER_OPTIONS = [(preset.label, preset.provider_type) for preset in PRESETS.values()]


class ChatWorker(QThread):
    finished_text = pyqtSignal(str)
    failed = pyqtSignal(str)

    def __init__(self, registry: ProviderRegistry, profile: ProviderProfile, messages: list[dict]):
        super().__init__()
        self.registry = registry
        self.profile = profile
        self.messages = messages

    def run(self) -> None:
        try:
            provider = self.registry.create_provider(self.profile)
            self.finished_text.emit(provider.complete(self.messages))
        except Exception as exc:
            self.failed.emit(str(exc))


class ModelFetchWorker(QThread):
    finished_models = pyqtSignal(list)
    failed = pyqtSignal(str)

    def __init__(self, registry: ProviderRegistry, profile: ProviderProfile):
        super().__init__()
        self.registry = registry
        self.profile = profile

    def run(self) -> None:
        try:
            provider = self.registry.create_provider(self.profile)
            self.finished_models.emit(provider.list_models())
        except Exception as exc:
            self.failed.emit(str(exc))


class SettingsDialog(QDialog):
    def __init__(self, config: AppConfig, registry: ProviderRegistry, parent=None):
        super().__init__(parent)
        self.setWindowTitle("AI 截图助手设置")
        self.resize(640, 360)
        self.registry = registry
        self.working_copy = AppConfig.from_dict(config.to_dict())
        self._loading = False
        self.fetch_worker: ModelFetchWorker | None = None

        self.profile_selector = QComboBox()
        self.profile_selector.currentIndexChanged.connect(self.on_profile_changed)
        add_profile_btn = QPushButton("新增")
        add_profile_btn.clicked.connect(self.add_profile)
        delete_profile_btn = QPushButton("删除")
        delete_profile_btn.clicked.connect(self.delete_profile)

        profile_row = QHBoxLayout()
        profile_row.addWidget(self.profile_selector, 1)
        profile_row.addWidget(add_profile_btn)
        profile_row.addWidget(delete_profile_btn)

        self.name_input = QLineEdit()
        self.provider_type_combo = QComboBox()
        for label, code in PROVIDER_OPTIONS:
            self.provider_type_combo.addItem(label, code)

        self.api_key_input = QLineEdit()
        self.base_url_input = QLineEdit()
        self.model_input = QComboBox()
        self.model_input.setEditable(True)
        self.shortcut_input = QLineEdit(self.working_copy.shortcut)
        self.theme_combo = QComboBox()
        self.theme_combo.addItem("深色", "dark")
        self.theme_combo.addItem("浅色", "light")
        self.opacity_combo = QComboBox()
        for value in (0.75, 0.82, 0.88, 0.92, 0.96):
            self.opacity_combo.addItem(f"{int(value * 100)}%", value)

        fetch_models_btn = QPushButton("检测模型")
        fetch_models_btn.clicked.connect(self.fetch_models)
        self.fetch_models_btn = fetch_models_btn
        self.provider_type_combo.currentIndexChanged.connect(self.on_provider_type_changed)

        model_row = QHBoxLayout()
        model_row.addWidget(self.model_input, 1)
        model_row.addWidget(fetch_models_btn)

        self.profile_help = QLabel("一个服务预设 = 一套协议 + Base URL + API Key + 模型。")
        self.profile_help.setWordWrap(True)
        self.provider_help = QLabel("")
        self.provider_help.setWordWrap(True)

        form = QFormLayout()
        form.addRow("服务预设:", profile_row)
        form.addRow("", self.profile_help)
        form.addRow("显示名称:", self.name_input)
        form.addRow("协议类型:", self.provider_type_combo)
        form.addRow("", self.provider_help)
        form.addRow("API Key:", self.api_key_input)
        form.addRow("Base URL:", self.base_url_input)
        form.addRow("模型:", model_row)
        form.addRow("快捷键:", self.shortcut_input)
        form.addRow("悬浮窗主题:", self.theme_combo)
        form.addRow("悬浮窗透明度:", self.opacity_combo)

        save_btn = QPushButton("保存")
        save_btn.clicked.connect(self.accept)
        cancel_btn = QPushButton("取消")
        cancel_btn.clicked.connect(self.reject)

        footer = QHBoxLayout()
        footer.addStretch()
        footer.addWidget(cancel_btn)
        footer.addWidget(save_btn)

        layout = QVBoxLayout()
        layout.addLayout(form)
        layout.addLayout(footer)
        self.setLayout(layout)

        self.setStyleSheet(_settings_stylesheet())
        self.load_profiles()
        self.theme_combo.setCurrentIndex(0 if self.working_copy.theme == "dark" else 1)
        opacity_index = max(0, self.opacity_combo.findData(self.working_copy.opacity))
        self.opacity_combo.setCurrentIndex(opacity_index)
        self._last_index = self.profile_selector.currentIndex()

    def load_profiles(self) -> None:
        self._loading = True
        self.profile_selector.clear()
        for profile in self.working_copy.provider_profiles:
            self.profile_selector.addItem(profile.name)
        index = max(0, self.profile_selector.findText(self.working_copy.active_profile))
        self.profile_selector.setCurrentIndex(index)
        self.load_profile_into_form(self.working_copy.provider_profiles[index])
        self._loading = False

    def load_profile_into_form(self, profile: ProviderProfile) -> None:
        previous_loading = self._loading
        self._loading = True
        self.name_input.setText(profile.name)
        provider_index = max(0, self.provider_type_combo.findData(profile.provider_type))
        self.provider_type_combo.setCurrentIndex(provider_index)
        self.api_key_input.setText(profile.api_key)
        self.base_url_input.setText(profile.base_url)
        self.set_model_items([profile.model_name] if profile.model_name else [], profile.model_name)
        self._loading = previous_loading
        self.apply_preset_to_form(get_provider_preset(profile.provider_type), force=False)

    def save_form_into_profile(self, index: int) -> None:
        if index < 0 or index >= len(self.working_copy.provider_profiles):
            return
        self.working_copy.provider_profiles[index] = ProviderProfile(
            name=self.name_input.text().strip() or f"服务 {index + 1}",
            provider_type=self.provider_type_combo.currentData(),
            base_url=self.base_url_input.text().strip(),
            api_key=self.api_key_input.text().strip(),
            model_name=self.current_model_text(),
        )
        self.profile_selector.setItemText(index, self.working_copy.provider_profiles[index].name)

    def on_profile_changed(self, index: int) -> None:
        if self._loading or index < 0:
            return
        previous = getattr(self, "_last_index", index)
        self.save_form_into_profile(previous)
        self.load_profile_into_form(self.working_copy.provider_profiles[index])
        self._last_index = index

    def add_profile(self) -> None:
        self.save_form_into_profile(self.profile_selector.currentIndex())
        preset = get_provider_preset("openai_compatible")
        new_profile = ProviderProfile(
            name=f"服务 {len(self.working_copy.provider_profiles) + 1}",
            provider_type=preset.provider_type,
            base_url=preset.base_url,
            model_name=preset.default_model,
        )
        self.working_copy.provider_profiles.append(new_profile)
        self.profile_selector.addItem(new_profile.name)
        self.profile_selector.setCurrentIndex(self.profile_selector.count() - 1)

    def delete_profile(self) -> None:
        if len(self.working_copy.provider_profiles) <= 1:
            QMessageBox.warning(self, "无法删除", "至少保留一个服务预设。")
            return
        index = self.profile_selector.currentIndex()
        if index < 0:
            return
        self.working_copy.provider_profiles.pop(index)
        self.load_profiles()

    def build_current_profile(self) -> ProviderProfile:
        return ProviderProfile(
            name=self.name_input.text().strip() or "临时服务",
            provider_type=self.provider_type_combo.currentData(),
            base_url=self.base_url_input.text().strip(),
            api_key=self.api_key_input.text().strip(),
            model_name=self.current_model_text(),
        )

    def fetch_models(self) -> None:
        profile = self.build_current_profile()
        preset = get_provider_preset(profile.provider_type)
        if not profile.base_url:
            QMessageBox.warning(self, "无法检测", "请先确认 Base URL。")
            return
        if not preset.supports_model_listing:
            QMessageBox.information(self, "无需检测", "当前协议通常不提供模型枚举，请手动输入模型名。")
            return
        self.fetch_models_btn.setEnabled(False)
        self.fetch_models_btn.setText("获取中...")
        self.fetch_worker = ModelFetchWorker(self.registry, profile)
        self.fetch_worker.finished_models.connect(self.on_fetch_success)
        self.fetch_worker.failed.connect(self.on_fetch_failed)
        self.fetch_worker.start()

    def on_fetch_success(self, models: list[str]) -> None:
        self.fetch_models_btn.setEnabled(True)
        self.fetch_models_btn.setText("检测模型")
        if not models:
            QMessageBox.information(self, "模型列表", "当前协议未返回可用模型，请手动填写。")
            return
        current = self.current_model_text()
        self.set_model_items(models[:200], current if current in models else models[0])
        self.model_input.showPopup()

    def on_fetch_failed(self, error: str) -> None:
        self.fetch_models_btn.setEnabled(True)
        self.fetch_models_btn.setText("检测模型")
        QMessageBox.warning(self, "获取失败", error)

    def accept(self) -> None:
        index = self.profile_selector.currentIndex()
        self.save_form_into_profile(index)
        self.working_copy.active_profile = self.working_copy.provider_profiles[index].name
        self.working_copy.shortcut = self.shortcut_input.text().strip() or "alt+q"
        self.working_copy.theme = self.theme_combo.currentData()
        self.working_copy.opacity = float(self.opacity_combo.currentData())
        super().accept()

    def get_config(self) -> AppConfig:
        return self.working_copy

    def current_model_text(self) -> str:
        return self.model_input.currentText().strip()

    def set_model_items(self, models: list[str], current: str = "") -> None:
        self.model_input.blockSignals(True)
        self.model_input.clear()
        for model in models:
            if model:
                self.model_input.addItem(model)
        self.model_input.setCurrentText(current)
        self.model_input.blockSignals(False)

    def on_provider_type_changed(self, index: int) -> None:
        if self._loading or index < 0:
            return
        preset = get_provider_preset(self.provider_type_combo.currentData())
        self.apply_preset_to_form(preset, force=True)

    def apply_preset_to_form(self, preset: ProviderPreset, force: bool) -> None:
        self.provider_help.setText(preset.description)
        self.base_url_input.setPlaceholderText(preset.base_url)
        self.base_url_input.setToolTip("切换协议时会自动填充默认地址，你仍然可以手动改成中转平台地址。")
        model_line = self.model_input.lineEdit()
        if model_line is not None:
            model_line.setPlaceholderText(preset.model_hint)

        current_base = self.base_url_input.text().strip()
        current_model = self.current_model_text()
        if force or not current_base or current_base in {item.base_url for item in PRESETS.values()}:
            self.base_url_input.setText(preset.base_url)
        if force or not current_model or current_model in {item.default_model for item in PRESETS.values()}:
            self.set_model_items([], preset.default_model)
        if not self.name_input.text().strip() or self.name_input.text().strip() in {
            item.label for item in PRESETS.values()
        }:
            self.name_input.setText(preset.label)


class FloatingChatPanel(QWidget):
    recrop_requested = pyqtSignal()

    def __init__(self, registry: ProviderRegistry, session: ChatSession, profile: ProviderProfile):
        super().__init__()
        self.registry = registry
        self.session = session
        self.profile = profile
        self.current_pixmap = QPixmap()
        self.last_assistant_text = ""
        self.worker: ChatWorker | None = None
        self.drag_pos = None

        self.setWindowFlags(
            Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.Tool
        )
        self.resize(520, 560)

        self.status_label = QLabel("待发送")
        self.profile_label = QLabel(profile.name)
        self.profile_label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)

        header = QHBoxLayout()
        header.addWidget(self.status_label)
        header.addStretch()
        header.addWidget(self.profile_label)

        self.preview_label = QLabel("暂无图片")
        self.preview_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview_label.setMinimumHeight(150)

        self.chat_browser = QTextBrowser()
        self.chat_browser.setOpenExternalLinks(True)

        self.input_edit = QLineEdit()
        self.input_edit.setPlaceholderText("继续追问这张图…")
        self.input_edit.returnPressed.connect(self.send_followup)
        send_btn = QPushButton("发送")
        send_btn.clicked.connect(self.send_followup)

        input_row = QHBoxLayout()
        input_row.addWidget(self.input_edit, 1)
        input_row.addWidget(send_btn)

        copy_btn = QPushButton("复制回答")
        copy_btn.clicked.connect(self.copy_last_answer)
        recrop_btn = QPushButton("重新截图")
        recrop_btn.clicked.connect(self.recrop_requested.emit)
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.close)

        footer = QHBoxLayout()
        footer.addWidget(copy_btn)
        footer.addWidget(recrop_btn)
        footer.addStretch()
        footer.addWidget(close_btn)

        layout = QVBoxLayout()
        layout.addLayout(header)
        layout.addWidget(self.preview_label)
        layout.addWidget(self.chat_browser, 1)
        layout.addLayout(input_row)
        layout.addLayout(footer)
        self.setLayout(layout)

    def apply_theme(self, theme: str, opacity: float) -> None:
        self.setWindowOpacity(opacity)
        self.setStyleSheet(build_panel_stylesheet(theme))
        self.render_history()

    def set_context(self, pixmap: QPixmap, session: ChatSession, profile: ProviderProfile) -> None:
        self.current_pixmap = pixmap
        self.session = session
        self.profile = profile
        self.profile_label.setText(profile.name)
        self._refresh_preview()
        self.render_history()

    def _refresh_preview(self) -> None:
        if self.current_pixmap.isNull():
            self.preview_label.setText("暂无图片")
            return
        scaled = self.current_pixmap.scaled(
            self.preview_label.size().width() or 440,
            160,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.preview_label.setPixmap(scaled)

    def resizeEvent(self, event) -> None:  # noqa: N802
        super().resizeEvent(event)
        self._refresh_preview()

    def render_history(self) -> None:
        sections: list[str] = []
        for message in self.session.export_messages():
            if message["role"] == "user":
                body = html.escape(message.get("text", ""))
                sections.append(
                    f'<div class="bubble user"><div class="role">你</div><div class="body">{body}</div></div>'
                )
            else:
                rendered = markdown.markdown(
                    message.get("text", ""),
                    extensions=["fenced_code", "tables"],
                )
                sections.append(
                    f'<div class="bubble assistant"><div class="role">AI</div><div class="body">{rendered}</div></div>'
                )

        self.chat_browser.setHtml(
            """
            <style>
                body { font-family: "Segoe UI", sans-serif; }
                .bubble { margin: 12px 0; padding: 10px 12px; border-radius: 10px; }
                .role { font-weight: bold; margin-bottom: 8px; }
                .user { background: rgba(56, 139, 253, 0.12); }
                .assistant { background: rgba(128, 128, 128, 0.12); }
                pre { overflow-x: auto; padding: 10px; border-radius: 8px; }
                code { padding: 2px 4px; border-radius: 4px; }
                table { border-collapse: collapse; width: 100%; }
                th, td { border: 1px solid #6e7781; padding: 6px; text-align: left; }
            </style>
            """
            + "".join(sections)
        )
        self.chat_browser.verticalScrollBar().setValue(
            self.chat_browser.verticalScrollBar().maximum()
        )

    def start_default_analysis(self) -> None:
        self.status_label.setText("分析中…")
        self.render_history()
        self._start_request()

    def send_followup(self) -> None:
        if self.worker and self.worker.isRunning():
            return
        text = self.input_edit.text().strip()
        if not text:
            return
        self.session.add_user_message(text)
        self.input_edit.clear()
        self.status_label.setText("思考中…")
        self.render_history()
        self._start_request()

    def _start_request(self) -> None:
        self.worker = ChatWorker(self.registry, self.profile, self.session.export_messages())
        self.worker.finished_text.connect(self.on_response_ready)
        self.worker.failed.connect(self.on_response_failed)
        self.worker.start()

    def on_response_ready(self, text: str) -> None:
        self.last_assistant_text = text
        self.session.add_assistant_message(text)
        self.status_label.setText("分析完成")
        self.render_history()

    def on_response_failed(self, error: str) -> None:
        self.last_assistant_text = error
        self.session.add_assistant_message(f"请求失败：\n\n```\n{error}\n```")
        self.status_label.setText("请求失败")
        self.render_history()

    def copy_last_answer(self) -> None:
        if self.last_assistant_text:
            from PyQt6.QtWidgets import QApplication

            QApplication.clipboard().setText(self.last_assistant_text)

    def mousePressEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag_pos = event.globalPosition().toPoint()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if self.drag_pos is not None:
            delta = event.globalPosition().toPoint() - self.drag_pos
            self.move(self.pos() + delta)
            self.drag_pos = event.globalPosition().toPoint()

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        self.drag_pos = None

    def closeEvent(self, event) -> None:  # noqa: N802
        if self.worker and self.worker.isRunning():
            self.worker.terminate()
        super().closeEvent(event)


def build_panel_stylesheet(theme: str) -> str:
    if theme == "light":
        return """
            QWidget {
                background-color: #f7f8fa;
                color: #1f2328;
                border: 1px solid #d0d7de;
                border-radius: 12px;
            }
            QLabel, QLineEdit, QTextBrowser { color: #1f2328; }
            QTextBrowser, QLabel#preview {
                background-color: #ffffff;
                border: 1px solid #d8dee4;
                border-radius: 8px;
            }
            QLineEdit {
                background-color: #ffffff;
                border: 1px solid #d0d7de;
                border-radius: 8px;
                padding: 8px;
            }
            QPushButton {
                background-color: #ffffff;
                border: 1px solid #c7ccd1;
                border-radius: 8px;
                padding: 6px 14px;
                color: #1f2328;
            }
            QPushButton:hover { background-color: #f0f3f6; }
            pre, code { background-color: #f3f4f6; }
        """
    return """
        QWidget {
            background-color: #1f2329;
            color: #d0d7de;
            border: 1px solid #444c56;
            border-radius: 12px;
        }
        QLabel, QLineEdit, QTextBrowser { color: #d0d7de; }
        QTextBrowser, QLabel#preview {
            background-color: #2d333b;
            border: 1px solid #444c56;
            border-radius: 8px;
        }
        QLineEdit {
            background-color: #2d333b;
            border: 1px solid #57606a;
            border-radius: 8px;
            padding: 8px;
        }
        QPushButton {
            background-color: #2d333b;
            border: 1px solid #57606a;
            border-radius: 8px;
            padding: 6px 14px;
            color: #ffffff;
        }
        QPushButton:hover { background-color: #3a414a; }
        pre, code { background-color: #0d1117; }
    """


def _settings_stylesheet() -> str:
    return """
        QDialog {
            background-color: #1f2329;
            color: #d0d7de;
        }
        QLineEdit, QComboBox {
            background-color: #2d333b;
            color: #ffffff;
            border: 1px solid #57606a;
            border-radius: 6px;
            padding: 6px;
        }
        QPushButton {
            background-color: #2d333b;
            color: #ffffff;
            border: 1px solid #57606a;
            border-radius: 6px;
            padding: 6px 14px;
        }
        QPushButton:hover {
            background-color: #3a414a;
        }
        QLabel {
            color: #d0d7de;
        }
    """
