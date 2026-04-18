from __future__ import annotations

import base64
from dataclasses import dataclass, field
from typing import Any


@dataclass(slots=True)
class ChatSession:
    default_prompt: str
    messages: list[dict[str, Any]] = field(default_factory=list)
    _image_b64: str | None = None

    def set_image_bytes(self, image_bytes: bytes) -> None:
        self._image_b64 = base64.b64encode(image_bytes).decode("utf-8")

    def start_with_default_prompt(self) -> None:
        if not self._image_b64:
            raise ValueError("Image bytes must be set before starting a session.")
        self.messages = [
            {"role": "user", "text": self.default_prompt, "image_b64": self._image_b64}
        ]

    def replace_image(self, image_bytes: bytes) -> None:
        self.set_image_bytes(image_bytes)
        self.start_with_default_prompt()

    def add_user_message(self, text: str) -> None:
        self.messages.append({"role": "user", "text": text})

    def add_assistant_message(self, text: str) -> None:
        self.messages.append({"role": "assistant", "text": text})

    def export_messages(self) -> list[dict[str, Any]]:
        return [dict(message) for message in self.messages]
