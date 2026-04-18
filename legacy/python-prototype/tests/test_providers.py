import unittest
from unittest.mock import Mock

from ai_screenshot.config import ProviderProfile
from ai_screenshot.providers import OpenAIResponsesProvider, ProviderRegistry


class ProviderTests(unittest.TestCase):
    def test_registry_creates_expected_provider_types(self):
        registry = ProviderRegistry()

        openai = registry.create_provider(
            ProviderProfile(
                name='OpenAI Responses',
                provider_type='openai_responses',
                base_url='https://api.openai.com/v1/responses',
                api_key='sk-test',
                model_name='gpt-4.1-mini',
            )
        )
        gemini = registry.create_provider(
            ProviderProfile(
                name='Gemini',
                provider_type='gemini',
                base_url='https://generativelanguage.googleapis.com/v1beta',
                api_key='gemini-key',
                model_name='gemini-2.5-flash',
            )
        )

        self.assertEqual(openai.display_name, 'OpenAI Responses')
        self.assertEqual(gemini.display_name, 'Google Gemini')

    def test_openai_compatible_builds_chat_completions_payload(self):
        registry = ProviderRegistry()
        provider = registry.create_provider(
            ProviderProfile(
                name='DeepSeek',
                provider_type='openai_compatible',
                base_url='https://api.deepseek.com/v1/chat/completions',
                api_key='deepseek-key',
                model_name='deepseek-chat',
            )
        )

        payload = provider.build_payload(
            [
                {'role': 'user', 'text': '请分析这张图', 'image_b64': 'ZmFrZS1pbWFnZQ=='},
                {'role': 'assistant', 'text': '好的'},
                {'role': 'user', 'text': '继续说明重点'},
            ]
        )

        self.assertEqual(payload['model'], 'deepseek-chat')
        self.assertEqual(payload['messages'][0]['role'], 'user')
        self.assertEqual(payload['messages'][0]['content'][0]['type'], 'text')
        self.assertEqual(payload['messages'][0]['content'][1]['type'], 'image_url')
        self.assertEqual(payload['messages'][-1]['content'], '继续说明重点')

    def test_openai_responses_parses_stream_events(self):
        provider = OpenAIResponsesProvider(
            ProviderProfile(
                name='OpenAI Responses',
                provider_type='openai_responses',
                base_url='https://api.openai.com/v1/responses',
                api_key='sk-test',
                model_name='gpt-5.2',
            )
        )

        lines = [
            'event: response.output_text.delta',
            'data: {"type":"response.output_text.delta","delta":"O"}',
            '',
            'event: response.output_text.delta',
            'data: {"type":"response.output_text.delta","delta":"K"}',
            '',
            'event: response.completed',
            'data: {"type":"response.completed","response":{"output":[]}}',
        ]

        self.assertEqual(provider._parse_sse_text(lines), 'OK')

    def test_openai_responses_forces_utf8_for_event_stream(self):
        provider = OpenAIResponsesProvider(
            ProviderProfile(
                name='OpenAI Responses',
                provider_type='openai_responses',
                base_url='https://api.openai.com/v1/responses',
                api_key='sk-test',
                model_name='gpt-5.2',
            )
        )
        response = Mock()
        response.headers = {'content-type': 'text/event-stream'}
        response.encoding = 'ISO-8859-1'

        provider._ensure_utf8_response(response)

        self.assertEqual(response.encoding, 'utf-8')


if __name__ == '__main__':
    unittest.main()
