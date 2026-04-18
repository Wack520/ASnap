from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass, field
from typing import Any

from .provider_presets import get_provider_preset


CONFIG_FILE = os.path.join(os.path.expanduser("~"), ".ai_screenshot_config.json")


@dataclass(slots=True)
class ProviderProfile:
    name: str
    provider_type: str
    base_url: str
    api_key: str = ""
    model_name: str = ""
    max_tokens: int = 1500

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "ProviderProfile":
        return cls(
            name=data.get("name", "默认配置"),
            provider_type=data.get("provider_type", "openai_chat"),
            base_url=data.get("base_url", ""),
            api_key=data.get("api_key", ""),
            model_name=data.get("model_name", ""),
            max_tokens=int(data.get("max_tokens", 1500) or 1500),
        )


@dataclass(slots=True)
class AppConfig:
    provider_profiles: list[ProviderProfile] = field(default_factory=list)
    active_profile: str = "OpenAI Chat"
    shortcut: str = "alt+q"
    opacity: float = 0.92
    theme: str = "dark"

    def __post_init__(self) -> None:
        if not self.provider_profiles:
            self.provider_profiles = [default_openai_profile()]

    def get_active_profile(self) -> ProviderProfile:
        for profile in self.provider_profiles:
            if profile.name == self.active_profile:
                return profile
        return self.provider_profiles[0]

    def to_dict(self) -> dict[str, Any]:
        return {
            "provider_profiles": [profile.to_dict() for profile in self.provider_profiles],
            "active_profile": self.active_profile,
            "shortcut": self.shortcut,
            "opacity": self.opacity,
            "theme": self.theme,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AppConfig":
        profiles = [
            ProviderProfile.from_dict(item)
            for item in data.get("provider_profiles", []) or []
        ]
        if not profiles:
            profiles = [default_openai_profile()]
        return cls(
            provider_profiles=profiles,
            active_profile=data.get("active_profile", profiles[0].name),
            shortcut=data.get("shortcut", "alt+q"),
            opacity=float(data.get("opacity", 0.92) or 0.92),
            theme=data.get("theme", "dark"),
        )

    @classmethod
    def load(cls, path: str = CONFIG_FILE) -> "AppConfig":
        if not os.path.exists(path):
            return cls()
        try:
            with open(path, "r", encoding="utf-8") as file:
                return cls.from_dict(json.load(file))
        except Exception:
            return cls()

    def save(self, path: str = CONFIG_FILE) -> None:
        with open(path, "w", encoding="utf-8") as file:
            json.dump(self.to_dict(), file, ensure_ascii=False, indent=2)


def default_openai_profile() -> ProviderProfile:
    preset = get_provider_preset("openai_chat")
    return ProviderProfile(
        name=preset.label,
        provider_type=preset.provider_type,
        base_url=preset.base_url,
        model_name=preset.default_model,
    )
