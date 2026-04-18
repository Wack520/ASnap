import unittest

from ai_screenshot.provider_presets import ProviderPreset, get_provider_preset


class ProviderPresetTests(unittest.TestCase):
    def test_responses_preset_uses_responses_url(self):
        preset = get_provider_preset('openai_responses')
        self.assertEqual(preset.label, 'OpenAI Responses API')
        self.assertEqual(preset.base_url, 'https://api.openai.com/v1/responses')
        self.assertTrue(preset.supports_model_listing)

    def test_gemini_preset_uses_google_base_and_manual_hint(self):
        preset = get_provider_preset('gemini')
        self.assertEqual(preset.base_url, 'https://generativelanguage.googleapis.com/v1beta')
        self.assertIn('gemini', preset.model_hint.lower())

    def test_unknown_provider_falls_back_to_openai_compatible(self):
        preset = get_provider_preset('unknown')
        self.assertEqual(preset.provider_type, 'openai_compatible')


if __name__ == '__main__':
    unittest.main()
