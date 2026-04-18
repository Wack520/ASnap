import unittest
from unittest.mock import Mock, patch

import requests

from ai_screenshot.config import ProviderProfile
from ai_screenshot.providers import OpenAIResponsesProvider, OpenAIChatProvider, ProviderError


class ProviderErrorHandlingTests(unittest.TestCase):
    def test_non_json_success_body_raises_readable_error(self):
        provider = OpenAIResponsesProvider(
            ProviderProfile(
                name='OpenAI Responses',
                provider_type='openai_responses',
                base_url='https://example.com/v1/responses',
                api_key='sk-test',
                model_name='gpt-4.1-mini',
            )
        )

        fake_response = Mock()
        fake_response.raise_for_status.return_value = None
        fake_response.text = 'error upstream returned html body'
        fake_response.json.side_effect = ValueError('not json')
        fake_response.headers = {'content-type': 'text/plain'}

        with patch('ai_screenshot.providers.requests.post', return_value=fake_response):
            with self.assertRaises(ProviderError) as cm:
                provider.complete([{'role': 'user', 'text': 'hello'}])

        self.assertIn('返回的不是合法 JSON', str(cm.exception))
        self.assertIn('text/plain', str(cm.exception))

    def test_proxy_error_falls_back_to_direct_connection(self):
        provider = OpenAIChatProvider(
            ProviderProfile(
                name='OpenAI Chat',
                provider_type='openai_chat',
                base_url='https://example.com/v1/chat/completions',
                api_key='sk-test',
                model_name='gpt-4.1-mini',
            )
        )

        fake_response = Mock()
        fake_response.raise_for_status.return_value = None
        fake_response.json.return_value = {
            'choices': [{'message': {'content': 'OK'}}]
        }
        fake_response.headers = {'content-type': 'application/json'}

        session = Mock()
        session.__enter__ = Mock(return_value=session)
        session.__exit__ = Mock(return_value=False)
        session.post.return_value = fake_response

        with patch(
            'ai_screenshot.providers.requests.post',
            side_effect=requests.exceptions.ProxyError(
                "Unable to connect to proxy 127.0.0.1:7890"
            ),
        ), patch('ai_screenshot.providers.requests.Session', return_value=session):
            text = provider.complete([{'role': 'user', 'text': 'hello'}])

        self.assertEqual(text, 'OK')
        self.assertFalse(session.trust_env)


if __name__ == '__main__':
    unittest.main()
