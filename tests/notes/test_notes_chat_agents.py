from __future__ import annotations

import unittest
from unittest import mock

from scripts.notes.notes_chat_agents import make_adapter
from scripts.notes.notes_chat_core import ChatError


class NotesChatAgentsTest(unittest.TestCase):
    def test_make_claude_adapter_uses_default_command(self) -> None:
        with mock.patch.dict("os.environ", {}, clear=True):
            adapter = make_adapter("claude", None, 5.0)

        health = adapter.health()
        self.assertEqual(adapter.name, "claude")
        self.assertEqual(health["name"], "claude")
        self.assertEqual(health["command"], "claude")
        self.assertEqual(health["transport"], "claude-cli")

    def test_make_acp_adapter_uses_env_default_command(self) -> None:
        with mock.patch.dict("os.environ", {"NOTES_CHAT_ACP_CMD": "agent acp"}, clear=True):
            adapter = make_adapter("acp", None, 5.0)

        health = adapter.health()
        self.assertEqual(adapter.name, "acp")
        self.assertEqual(health["name"], "acp")
        self.assertEqual(health["command"], "agent acp")
        self.assertEqual(health["transport"], "acp-stdio")

    def test_command_override_wins(self) -> None:
        with mock.patch.dict(
            "os.environ",
            {"NOTES_CHAT_CLAUDE_CMD": "env-claude", "NOTES_CHAT_ACP_CMD": "env-acp"},
            clear=True,
        ):
            claude = make_adapter("claude", "custom claude", 5.0)
            acp = make_adapter("acp", "custom acp", 5.0)

        self.assertEqual(claude.health()["command"], "custom claude")
        self.assertEqual(acp.health()["command"], "custom acp")

    def test_unknown_agent_raises_chat_error(self) -> None:
        with self.assertRaises(ChatError):
            make_adapter("codex", None, 5.0)


if __name__ == "__main__":
    unittest.main()
