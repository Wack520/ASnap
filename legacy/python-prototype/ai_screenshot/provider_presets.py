from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ProviderPreset:
    provider_type: str
    label: str
    base_url: str
    default_model: str
    model_hint: str
    supports_model_listing: bool
    description: str


PRESETS: dict[str, ProviderPreset] = {
    "openai_chat": ProviderPreset(
        provider_type="openai_chat",
        label="OpenAI Chat Completions",
        base_url="https://api.openai.com/v1/chat/completions",
        default_model="gpt-4.1-mini",
        model_hint="例如 gpt-4.1-mini / gpt-4o",
        supports_model_listing=True,
        description="OpenAI 标准 Chat Completions，也兼容大量国内中转平台。",
    ),
    "openai_responses": ProviderPreset(
        provider_type="openai_responses",
        label="OpenAI Responses API",
        base_url="https://api.openai.com/v1/responses",
        default_model="gpt-4.1-mini",
        model_hint="例如 gpt-4.1-mini / gpt-4.1",
        supports_model_listing=True,
        description="OpenAI 新版 Responses 接口；部分兼容平台也支持。",
    ),
    "openai_compatible": ProviderPreset(
        provider_type="openai_compatible",
        label="OpenAI-Compatible / DeepSeek",
        base_url="https://api.deepseek.com/v1/chat/completions",
        default_model="deepseek-chat",
        model_hint="例如 deepseek-chat / qwen-max / 自建服务模型名",
        supports_model_listing=True,
        description="DeepSeek、OneAPI、NewAPI、Ollama(OpenAI 模式) 等。",
    ),
    "gemini": ProviderPreset(
        provider_type="gemini",
        label="Google Gemini",
        base_url="https://generativelanguage.googleapis.com/v1beta",
        default_model="gemini-2.5-flash",
        model_hint="例如 gemini-2.5-flash",
        supports_model_listing=True,
        description="Google Gemini 原生协议。",
    ),
    "claude": ProviderPreset(
        provider_type="claude",
        label="Anthropic Claude",
        base_url="https://api.anthropic.com/v1/messages",
        default_model="claude-sonnet-4-0",
        model_hint="例如 claude-sonnet-4-0",
        supports_model_listing=False,
        description="Anthropic Claude 原生 Messages 接口。",
    ),
}


def get_provider_preset(provider_type: str) -> ProviderPreset:
    return PRESETS.get(provider_type, PRESETS["openai_compatible"])
