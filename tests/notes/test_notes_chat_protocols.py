from __future__ import annotations

import unittest

from scripts.notes.notes_chat_protocols import (
    AcpStdioProtocol,
    ClaudeCliProtocol,
    extract_acp_update_text,
    extract_claude_result_text,
    extract_claude_stream_text,
    extract_content_text,
)


class NotesChatProtocolsTest(unittest.TestCase):
    def test_protocol_health_does_not_include_adapter_name(self) -> None:
        self.assertNotIn("name", ClaudeCliProtocol("claude", 5.0).health())
        self.assertNotIn("name", AcpStdioProtocol("agent acp", 5.0).health())

    def test_extract_content_text_handles_common_shapes(self) -> None:
        self.assertEqual(extract_content_text("hello"), "hello")
        self.assertEqual(
            extract_content_text([
                {"type": "text", "text": "hello "},
                {"delta": "world"},
                {"message": {"content": [{"text": "!"}]}},
            ]),
            "hello world!",
        )
        self.assertEqual(extract_content_text({"resource": {"text": "resource text"}}), "resource text")

    def test_extract_claude_stream_text_handles_stream_events(self) -> None:
        self.assertEqual(
            extract_claude_stream_text(
                {
                    "type": "stream_event",
                    "event": {
                        "type": "content_block_delta",
                        "delta": {"type": "text_delta", "text": "delta"},
                    },
                }
            ),
            "delta",
        )
        self.assertEqual(
            extract_claude_stream_text(
                {
                    "type": "assistant",
                    "message": {"content": [{"type": "text", "text": "assistant text"}]},
                }
            ),
            "assistant text",
        )

    def test_extract_claude_result_text_handles_string_and_content_result(self) -> None:
        self.assertEqual(extract_claude_result_text({"type": "result", "result": "final"}), "final")
        self.assertEqual(
            extract_claude_result_text({"result": {"content": [{"text": "nested final"}]}}),
            "nested final",
        )

    def test_extract_acp_update_text_handles_agent_message_chunks(self) -> None:
        self.assertEqual(
            extract_acp_update_text(
                {
                    "sessionId": "session-1",
                    "update": {
                        "sessionUpdate": "agent_message_chunk",
                        "content": [{"type": "text", "text": "hello"}, {"text": " acp"}],
                    },
                }
            ),
            "hello acp",
        )
        self.assertEqual(
            extract_acp_update_text({"update": {"sessionUpdate": "tool_call", "content": {"text": "hidden"}}}),
            "",
        )


if __name__ == "__main__":
    unittest.main()
