from __future__ import annotations

import sys
from pathlib import Path
import unittest

from scripts.notes.notes_chat_protocols import (
    AcpStdioProtocol,
    ClaudeCliProtocol,
    McpStdioProtocol,
    extract_acp_update_text,
    extract_claude_result_text,
    extract_claude_stream_text,
    extract_content_text,
)
from scripts.notes.notes_chat_core import ChatError, ChatRequest, DocumentContext


FAKE_MCP_SERVER = Path(__file__).resolve().parent / "fakes" / "fake_mcp_server.py"


def make_request() -> ChatRequest:
    return ChatRequest(
        document=DocumentContext(
            rel_path="README.md",
            title="Notes",
            text="# Notes\nBody",
            truncated=False,
        ),
        message="What is this?",
        selected_text="",
        history=[],
    )


class NotesChatProtocolsTest(unittest.TestCase):
    def test_protocol_health_does_not_include_adapter_name(self) -> None:
        self.assertNotIn("name", ClaudeCliProtocol("claude", 5.0).health())
        self.assertNotIn("name", AcpStdioProtocol("agent acp", 5.0).health())
        self.assertNotIn("name", McpStdioProtocol([sys.executable, str(FAKE_MCP_SERVER)], 5.0).health())

    def test_mcp_stdio_protocol_ok_fake_returns_text(self) -> None:
        protocol = McpStdioProtocol([sys.executable, str(FAKE_MCP_SERVER)], 5.0)

        self.assertEqual("".join(protocol.stream(make_request())), "fake mcp response with prompt")

    def test_mcp_stdio_protocol_missing_compatible_tool_raises_502(self) -> None:
        protocol = McpStdioProtocol([sys.executable, str(FAKE_MCP_SERVER), "--mode", "no-tool"], 5.0)

        with self.assertRaises(ChatError) as raised:
            list(protocol.stream(make_request()))

        self.assertEqual(raised.exception.status, 502)
        self.assertIn("No compatible Codex MCP tool", raised.exception.message)
        self.assertIn("codex-exec", raised.exception.message)

    def test_mcp_stdio_protocol_early_exit_raises_502(self) -> None:
        protocol = McpStdioProtocol([sys.executable, str(FAKE_MCP_SERVER), "--mode", "early-exit"], 5.0)

        with self.assertRaises(ChatError) as raised:
            list(protocol.stream(make_request()))

        self.assertEqual(raised.exception.status, 502)

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
