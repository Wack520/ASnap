from __future__ import annotations

from abc import ABC, abstractmethod
import json
import os
from typing import Any

import requests

from .config import ProviderProfile


Message = dict[str, Any]


class ProviderError(RuntimeError):
    """Raised when a provider request fails."""


class BaseProvider(ABC):
    display_name = "Base Provider"
    supports_model_listing = True

    def __init__(self, profile: ProviderProfile):
        self.profile = profile

    @abstractmethod
    def build_payload(self, messages: list[Message]) -> dict[str, Any]:
        raise NotImplementedError

    def complete(self, messages: list[Message]) -> str:
        response = self._request(
            "post",
            self.request_url,
            headers=self.headers,
            json=self.build_payload(messages),
            timeout=90,
        )
        payload = self._load_json_payload(response)

        if isinstance(payload, dict) and payload.get("error"):
            raise ProviderError(self.format_error_payload(payload["error"]))
        return self.parse_response(payload)

    def _load_json_payload(self, response: requests.Response) -> dict[str, Any]:
        self._ensure_utf8_response(response)
        try:
            response.raise_for_status()
        except Exception as exc:
            raise ProviderError(self._response_preview(response) or str(exc)) from exc
        try:
            return response.json()
        except Exception as exc:
            content_type = response.headers.get("content-type", "unknown")
            preview = self._response_preview(response)
            raise ProviderError(
                f"模型接口返回的不是合法 JSON。\n"
                f"Content-Type: {content_type}\n"
                f"Body 预览: {preview or '<empty>'}"
            ) from exc

    def _response_preview(self, response: requests.Response, limit: int = 300) -> str:
        try:
            body = response.text or ""
        except Exception:
            body = ""
        return body.strip()[:limit]

    def _ensure_utf8_response(self, response: requests.Response) -> None:
        content_type = (response.headers.get("content-type") or "").lower()
        if "application/json" in content_type or "text/event-stream" in content_type:
            response.encoding = "utf-8"

    def _request(self, method: str, url: str, **kwargs) -> requests.Response:
        requester = getattr(requests, method)
        try:
            return requester(url, **kwargs)
        except requests.exceptions.ProxyError as exc:
            if self._has_proxy_env():
                return self._request_without_env(method, url, original_exc=exc, **kwargs)
            raise ProviderError(self._format_request_exception(exc)) from exc
        except requests.exceptions.ConnectionError as exc:
            if self._has_localhost_proxy_env():
                return self._request_without_env(method, url, original_exc=exc, **kwargs)
            raise ProviderError(self._format_request_exception(exc)) from exc
        except requests.exceptions.RequestException as exc:
            raise ProviderError(self._format_request_exception(exc)) from exc

    def _request_without_env(
        self,
        method: str,
        url: str,
        *,
        original_exc: Exception,
        **kwargs,
    ) -> requests.Response:
        try:
            with requests.Session() as session:
                session.trust_env = False
                requester = getattr(session, method)
                return requester(url, **kwargs)
        except requests.exceptions.RequestException as exc:
            proxy_hint = self._proxy_env_hint()
            message = "检测到本机代理不可用，已尝试直连但仍失败。"
            if proxy_hint:
                message += f"\n当前代理环境变量: {proxy_hint}"
            message += (
                f"\n代理错误: {self._format_request_exception(original_exc)}"
                f"\n直连错误: {self._format_request_exception(exc)}"
            )
            raise ProviderError(message) from exc

    def _proxy_env_hint(self) -> str:
        proxy_keys = (
            "HTTPS_PROXY",
            "HTTP_PROXY",
            "ALL_PROXY",
            "https_proxy",
            "http_proxy",
            "all_proxy",
        )
        parts = [f"{key}={os.environ[key]}" for key in proxy_keys if os.environ.get(key)]
        return "; ".join(parts)

    def _has_proxy_env(self) -> bool:
        return bool(self._proxy_env_hint())

    def _has_localhost_proxy_env(self) -> bool:
        return any(
            token in self._proxy_env_hint().lower()
            for token in ("127.0.0.1", "localhost")
        )

    def _format_request_exception(self, exc: Exception) -> str:
        message = str(exc).strip()
        if not message:
            message = exc.__class__.__name__
        if "127.0.0.1" in message or "localhost" in message:
            return f"{message}\n检测到请求被本机代理接管，但代理当前不可用。"
        return message

    def list_models(self) -> list[str]:
        if not self.supports_model_listing:
            return []
        response = self._request("get", self.models_url, headers=self.headers, timeout=20)
        try:
            response.raise_for_status()
        except Exception as exc:
            raise ProviderError(response.text or str(exc)) from exc
        payload = response.json()
        if isinstance(payload, dict) and "data" in payload:
            return [
                item["id"]
                for item in payload["data"]
                if isinstance(item, dict) and item.get("id")
            ]
        if isinstance(payload, dict) and "models" in payload:
            return [
                item["name"]
                for item in payload["models"]
                if isinstance(item, dict) and item.get("name")
            ]
        if isinstance(payload, list):
            return [
                item.get("name") or item.get("id")
                for item in payload
                if isinstance(item, dict) and (item.get("name") or item.get("id"))
            ]
        return []

    @property
    def request_url(self) -> str:
        return self.profile.base_url.rstrip("/")

    @property
    def models_url(self) -> str:
        base_root = self._base_root
        return f"{base_root}/models"

    @property
    def headers(self) -> dict[str, str]:
        if not self.profile.api_key:
            return {}
        return {"Authorization": f"Bearer {self.profile.api_key}"}

    @property
    def _base_root(self) -> str:
        url = self.profile.base_url.rstrip("/")
        for suffix in ("/chat/completions", "/responses", "/messages"):
            if url.endswith(suffix):
                return url[: -len(suffix)]
        return url

    def parse_response(self, payload: dict[str, Any]) -> str:
        raise NotImplementedError

    def format_error_payload(self, error_payload: Any) -> str:
        if isinstance(error_payload, dict):
            message = error_payload.get("message") or str(error_payload)
            error_type = error_payload.get("type")
            error_code = error_payload.get("code")
            parts = [f"接口返回错误：{message}"]
            if error_type:
                parts.append(f"type={error_type}")
            if error_code:
                parts.append(f"code={error_code}")
            if (
                self.profile.provider_type == "openai_responses"
                and error_code == "bad_response_body"
            ):
                parts.append(
                    "当前 /v1/responses 已接通，但这个模型或上游返回体不兼容。"
                    "请换支持 Responses 的模型，或改用 /chat/completions。"
                )
            return "\n".join(parts)
        return f"接口返回错误：{error_payload}"


class OpenAICompatibleProvider(BaseProvider):
    display_name = "OpenAI-Compatible"

    @property
    def request_url(self) -> str:
        url = self.profile.base_url.rstrip("/")
        if url.endswith("/chat/completions"):
            return url
        return f"{self._base_root}/chat/completions"

    def build_payload(self, messages: list[Message]) -> dict[str, Any]:
        return {
            "model": self.profile.model_name,
            "messages": [self._convert_message(message) for message in messages],
            "max_tokens": self.profile.max_tokens,
            "stream": False,
        }

    def _convert_message(self, message: Message) -> dict[str, Any]:
        role = message["role"]
        text = message.get("text", "")
        image_b64 = message.get("image_b64")
        if image_b64:
            return {
                "role": role,
                "content": [
                    {"type": "text", "text": text},
                    {
                        "type": "image_url",
                        "image_url": {"url": f"data:image/png;base64,{image_b64}"},
                    },
                ],
            }
        return {"role": role, "content": text}

    def parse_response(self, payload: dict[str, Any]) -> str:
        choices = payload.get("choices", [])
        if not choices:
            return "模型返回了空响应。"
        message = choices[0].get("message", {})
        content = message.get("content", "")
        if isinstance(content, str):
            return content
        if isinstance(content, list):
            return "\n".join(
                item.get("text", "")
                for item in content
                if isinstance(item, dict) and item.get("text")
            ).strip()
        return str(content)


class OpenAIChatProvider(OpenAICompatibleProvider):
    display_name = "OpenAI Chat Completions"


class OpenAIResponsesProvider(BaseProvider):
    display_name = "OpenAI Responses"

    @property
    def request_url(self) -> str:
        url = self.profile.base_url.rstrip("/")
        if url.endswith("/responses"):
            return url
        return f"{self._base_root}/responses"

    def build_payload(self, messages: list[Message]) -> dict[str, Any]:
        converted = []
        for message in messages:
            parts: list[dict[str, Any]] = [{"type": "input_text", "text": message.get("text", "")}]
            if message.get("image_b64"):
                parts.append(
                    {
                        "type": "input_image",
                        "image_url": f"data:image/png;base64,{message['image_b64']}",
                    }
                )
            converted.append({"role": message["role"], "content": parts})
        return {"model": self.profile.model_name, "input": converted}

    def complete(self, messages: list[Message]) -> str:
        stream_payload = self.build_payload(messages) | {"stream": True}
        stream_error: str | None = None
        try:
            text = self._complete_stream(stream_payload)
            if text:
                return text
        except Exception as exc:
            stream_error = str(exc)

        try:
            payload = super().complete(messages)
            if payload and payload not in {
                "模型返回了空响应。",
                "模型已生成输出 token，但当前中转没有在 JSON 结果里返回正文。",
            }:
                return payload
        except Exception as exc:
            fallback_error = str(exc)
            if stream_error and fallback_error != stream_error:
                raise ProviderError(
                    "Responses 流式解析失败，非流式回退也失败：\n"
                    f"- stream: {stream_error}\n"
                    f"- json: {fallback_error}"
                ) from exc
            raise

        if stream_error:
            raise ProviderError(
                "Responses 已返回完成状态，但非流式结果里没有正文；"
                "当前中转需要走流式事件解析。\n"
                f"stream: {stream_error}"
            )
        return "模型返回了空响应。"

    def _complete_stream(self, payload: dict[str, Any]) -> str:
        response = self._request(
            "post",
            self.request_url,
            headers=self.headers,
            json=payload,
            timeout=90,
            stream=True,
        )
        self._ensure_utf8_response(response)
        content_type = response.headers.get("content-type", "")
        if "text/event-stream" not in content_type.lower():
            return self.parse_response(self._load_json_payload(response))

        if not response.ok:
            raise ProviderError(self._response_preview(response) or f"HTTP {response.status_code}")

        return self._parse_sse_text(response.iter_lines(decode_unicode=True))

    def _parse_sse_text(self, lines) -> str:
        delta_parts: list[str] = []
        done_parts: list[str] = []
        current_event = ""

        for raw_line in lines:
            if raw_line is None:
                continue
            line = raw_line.strip()
            if not line:
                continue
            if line.startswith("event:"):
                current_event = line[6:].strip()
                continue
            if line.startswith("data:"):
                data_text = line[5:].strip()
            else:
                data_text = line

            if data_text == "[DONE]":
                break

            try:
                payload_item = json.loads(data_text)
            except json.JSONDecodeError:
                continue

            if isinstance(payload_item, dict) and payload_item.get("error"):
                raise ProviderError(self.format_error_payload(payload_item["error"]))

            event_type = payload_item.get("type") or current_event
            if event_type == "response.output_text.delta":
                delta = payload_item.get("delta", "")
                if delta:
                    delta_parts.append(delta)
                continue
            if event_type == "response.output_text.done":
                text = payload_item.get("text", "")
                if text:
                    done_parts.append(text)
                continue
            if event_type == "response.output_item.done":
                done_parts.extend(_collect_output_texts(payload_item.get("item", {})))
                continue
            if event_type == "response.completed":
                done_parts.extend(_collect_output_texts(payload_item.get("response", {})))

        text = "".join(delta_parts).strip()
        if text:
            return text

        text = "\n".join(part for part in done_parts if part).strip()
        if text:
            return text

        raise ProviderError("Responses 流式接口已返回完成事件，但没有可解析的正文。")

    def parse_response(self, payload: dict[str, Any]) -> str:
        if payload.get("output_text"):
            return payload["output_text"]
        if isinstance(payload.get("text"), str) and payload["text"].strip():
            return payload["text"].strip()
        if payload.get("choices"):
            return OpenAICompatibleProvider.parse_response(self, payload)
        collected = _collect_output_texts(payload)
        if collected:
            return "\n".join(collected).strip()
        if payload.get("usage", {}).get("output_tokens"):
            return "模型已生成输出 token，但当前中转没有在 JSON 结果里返回正文。"
        return "模型返回了空响应。"


class GeminiProvider(BaseProvider):
    display_name = "Google Gemini"

    @property
    def request_url(self) -> str:
        base_root = self._base_root.rstrip("/")
        return (
            f"{base_root}/models/{self.profile.model_name}:generateContent"
            f"?key={self.profile.api_key}"
        )

    @property
    def models_url(self) -> str:
        return f"{self._base_root.rstrip('/')}/models?key={self.profile.api_key}"

    @property
    def headers(self) -> dict[str, str]:
        return {"Content-Type": "application/json"}

    def build_payload(self, messages: list[Message]) -> dict[str, Any]:
        contents = []
        for message in messages:
            parts: list[dict[str, Any]] = []
            if message.get("text"):
                parts.append({"text": message["text"]})
            if message.get("image_b64"):
                parts.append(
                    {
                        "inline_data": {
                            "mime_type": "image/png",
                            "data": message["image_b64"],
                        }
                    }
                )
            contents.append(
                {"role": "model" if message["role"] == "assistant" else "user", "parts": parts}
            )
        return {"contents": contents}

    def parse_response(self, payload: dict[str, Any]) -> str:
        texts: list[str] = []
        for candidate in payload.get("candidates", []) or []:
            for part in candidate.get("content", {}).get("parts", []) or []:
                if part.get("text"):
                    texts.append(part["text"])
        return "\n".join(texts).strip() or "模型返回了空响应。"


class ClaudeProvider(BaseProvider):
    display_name = "Anthropic Claude"
    supports_model_listing = False

    @property
    def request_url(self) -> str:
        url = self.profile.base_url.rstrip("/")
        if url.endswith("/messages"):
            return url
        return f"{self._base_root}/messages"

    @property
    def headers(self) -> dict[str, str]:
        return {
            "x-api-key": self.profile.api_key,
            "anthropic-version": "2023-06-01",
            "content-type": "application/json",
        }

    def build_payload(self, messages: list[Message]) -> dict[str, Any]:
        converted = []
        for message in messages:
            content: list[dict[str, Any]] = []
            if message.get("text"):
                content.append({"type": "text", "text": message["text"]})
            if message.get("image_b64"):
                content.append(
                    {
                        "type": "image",
                        "source": {
                            "type": "base64",
                            "media_type": "image/png",
                            "data": message["image_b64"],
                        },
                    }
                )
            converted.append({"role": message["role"], "content": content})
        return {
            "model": self.profile.model_name,
            "max_tokens": self.profile.max_tokens,
            "messages": converted,
        }

    def parse_response(self, payload: dict[str, Any]) -> str:
        texts = [
            item.get("text", "")
            for item in payload.get("content", []) or []
            if isinstance(item, dict) and item.get("type") == "text"
        ]
        return "\n".join(part for part in texts if part).strip() or "模型返回了空响应。"


class ProviderRegistry:
    def __init__(self) -> None:
        self.providers = {
            "openai_chat": OpenAIChatProvider,
            "openai_responses": OpenAIResponsesProvider,
            "openai_compatible": OpenAICompatibleProvider,
            "gemini": GeminiProvider,
            "claude": ClaudeProvider,
        }

    def create_provider(self, profile: ProviderProfile) -> BaseProvider:
        provider_cls = self.providers.get(profile.provider_type)
        if provider_cls is None:
            raise ProviderError(f"不支持的 Provider 类型: {profile.provider_type}")
        return provider_cls(profile)


def _collect_output_texts(payload: dict[str, Any]) -> list[str]:
    collected: list[str] = []
    for block in payload.get("output", []) or []:
        for item in block.get("content", []) or []:
            if item.get("type") == "output_text" and item.get("text"):
                collected.append(item["text"])
    for item in payload.get("content", []) or []:
        if item.get("type") == "output_text" and item.get("text"):
            collected.append(item["text"])
    return collected
