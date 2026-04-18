import unittest

from ai_screenshot.config import AppConfig, ProviderProfile


class ConfigTests(unittest.TestCase):
    def test_roundtrip_preserves_profiles_and_theme(self):
        config = AppConfig(
            provider_profiles=[
                ProviderProfile(
                    name='主 OpenAI',
                    provider_type='openai_chat',
                    base_url='https://api.openai.com/v1/chat/completions',
                    api_key='sk-test',
                    model_name='gpt-4.1',
                ),
                ProviderProfile(
                    name='DeepSeek',
                    provider_type='openai_compatible',
                    base_url='https://api.deepseek.com/v1/chat/completions',
                    api_key='deepseek-key',
                    model_name='deepseek-chat',
                ),
            ],
            active_profile='DeepSeek',
            shortcut='alt+q',
            opacity=0.8,
            theme='light',
        )

        restored = AppConfig.from_dict(config.to_dict())

        self.assertEqual(restored.active_profile, 'DeepSeek')
        self.assertEqual(restored.theme, 'light')
        self.assertEqual(restored.opacity, 0.8)
        self.assertEqual(len(restored.provider_profiles), 2)
        self.assertEqual(restored.get_active_profile().model_name, 'deepseek-chat')

    def test_missing_active_profile_falls_back_to_first(self):
        config = AppConfig(
            provider_profiles=[
                ProviderProfile(
                    name='Gemini',
                    provider_type='gemini',
                    base_url='https://generativelanguage.googleapis.com/v1beta',
                    api_key='gemini-key',
                    model_name='gemini-2.5-flash',
                )
            ],
            active_profile='不存在',
        )

        self.assertEqual(config.get_active_profile().name, 'Gemini')


if __name__ == '__main__':
    unittest.main()
