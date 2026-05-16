from __future__ import annotations

import unittest
from unittest import mock

from scripts.notes.notes_chat_agents import make_adapter
from scripts.notes.notes_chat_core import ChatError


class NotesChatAgentsTest(unittest.TestCase):
    def test_make_codex_adapter_defaults_to_mcp_stdio(self) -> None:
        with mock.patch.dict("os.environ", {}, clear=True):
            adapter = make_adapter("codex", None, 5.0)

        health = adapter.health()
        self.assertEqual(adapter.name, "codex")
        self.assertEqual(health["name"], "codex")
        self.assertEqual(health["command"], "codex mcp-server")
        self.assertEqual(health["transport"], "mcp-stdio")

    def test_make_codex_adapter_uses_env_default_command(self) -> None:
        with mock.patch.dict("os.environ", {"NOTES_CHAT_CODEX_CMD": "custom-codex mcp"}, clear=True):
            adapter = make_adapter("codex", None, 5.0)

        health = adapter.health()
        self.assertEqual(adapter.name, "codex")
        self.assertEqual(health["name"], "codex")
        self.assertEqual(health["command"], "custom-codex mcp")
        self.assertEqual(health["transport"], "mcp-stdio")

    def test_make_codex_exec_adapter_defaults_to_cli_json(self) -> None:
        with mock.patch.dict("os.environ", {}, clear=True):
            adapter = make_adapter("codex-exec", None, 12.0)

        health = adapter.health()
        self.assertEqual(adapter.name, "codex-exec")
        self.assertEqual(health["name"], "codex-exec")
        self.assertEqual(
            health["command"],
            "codex exec --json --ephemeral --sandbox read-only",
        )
        self.assertEqual(health["transport"], "cli-json")

    def test_make_codex_exec_adapter_uses_env_default_command(self) -> None:
        with mock.patch.dict("os.environ", {"NOTES_CHAT_CODEX_EXEC_CMD": "custom-codex exec"}, clear=True):
            adapter = make_adapter("codex-exec", None, 5.0)

        health = adapter.health()
        self.assertEqual(adapter.name, "codex-exec")
        self.assertEqual(health["name"], "codex-exec")
        self.assertEqual(health["command"], "custom-codex exec")
        self.assertEqual(health["transport"], "cli-json")

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
            codex = make_adapter("codex", "custom codex", 5.0)
            codex_exec = make_adapter("codex-exec", "custom codex exec", 5.0)
            claude = make_adapter("claude", "custom claude", 5.0)
            acp = make_adapter("acp", "custom acp", 5.0)

        self.assertEqual(codex.health()["command"], "custom codex")
        self.assertEqual(codex_exec.health()["command"], "custom codex exec")
        self.assertEqual(claude.health()["command"], "custom claude")
        self.assertEqual(acp.health()["command"], "custom acp")

    def test_unknown_agent_raises_chat_error(self) -> None:
        with self.assertRaises(ChatError):
            make_adapter("unknown", None, 5.0)


if __name__ == "__main__":
    unittest.main()
