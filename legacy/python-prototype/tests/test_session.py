import unittest

from ai_screenshot.session import ChatSession


class SessionTests(unittest.TestCase):
    def test_session_keeps_image_for_first_turn_and_followups(self):
        session = ChatSession(default_prompt='请分析这张截图')
        session.set_image_bytes(b'png-bytes')
        session.start_with_default_prompt()
        session.add_assistant_message('这是一个 IDE 报错界面。')
        session.add_user_message('继续看左上角的错误')

        exported = session.export_messages()

        self.assertEqual(exported[0]['role'], 'user')
        self.assertEqual(exported[0]['text'], '请分析这张截图')
        self.assertEqual(exported[0]['image_b64'], 'cG5nLWJ5dGVz')
        self.assertEqual(exported[-1]['text'], '继续看左上角的错误')
        self.assertNotIn('image_b64', exported[-1])

    def test_replacing_image_resets_history_to_new_default_prompt(self):
        session = ChatSession(default_prompt='请分析这张截图')
        session.set_image_bytes(b'old')
        session.start_with_default_prompt()
        session.add_assistant_message('old answer')

        session.replace_image(b'new')

        exported = session.export_messages()
        self.assertEqual(len(exported), 1)
        self.assertEqual(exported[0]['image_b64'], 'bmV3')
        self.assertEqual(exported[0]['text'], '请分析这张截图')


if __name__ == '__main__':
    unittest.main()
