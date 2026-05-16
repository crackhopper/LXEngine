from __future__ import annotations

import pathlib
import re
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SUPPORTED_AGENTS = {"codex", "codex-exec", "claude", "acp"}


class NotesServeScriptsTest(unittest.TestCase):
    def test_shell_script_defaults_to_codex_and_accepts_supported_agents(self) -> None:
        text = (REPO_ROOT / "scripts/notes/serve_site.sh").read_text(encoding="utf-8")
        self.assertIn('CHAT_AGENT="${NOTES_CHAT_AGENT:-codex}"', text)
        self.assertIn('NOTES_CHAT_CLIENT_HOST="${CHAT_CLIENT_HOST:-}"', text)
        self.assertIn('NOTES_CHAT_CLIENT_PORT="${CHAT_PORT:-}"', text)
        self.assertIn('--chat-client-host "${CHAT_CLIENT_HOST}"', text)
        self.assertIn('--chat-agent must be codex, codex-exec, claude, or acp', text)
        self.assertNotIn("must be claude or acp", text)
        for agent in SUPPORTED_AGENTS:
            self.assertIn(f'"{agent}"', text)

    def test_powershell_script_defaults_to_codex_and_accepts_supported_agents(self) -> None:
        text = (REPO_ROOT / "scripts/notes/serve_site.ps1").read_text(encoding="utf-8")
        self.assertIn('else { "codex" }', text)
        validate_set = re.search(r'\[ValidateSet\(([^\)]*)\)\]', text)
        self.assertIsNotNone(validate_set)
        values = validate_set.group(1)
        for agent in SUPPORTED_AGENTS:
            self.assertIn(f'"{agent}"', values)
        self.assertIn("NOTES_CHAT_CLIENT_HOST", text)
        self.assertIn("NOTES_CHAT_CLIENT_PORT", text)
        self.assertIn("--chat-client-host", text)

    def test_browser_chat_script_uses_generated_config_before_inference(self) -> None:
        text = (REPO_ROOT / "notes/assets/javascripts/notes-chat.js").read_text(encoding="utf-8")
        self.assertIn("window.NOTES_CHAT_CONFIG", text)
        self.assertIn("configuredEndpoint() || inferEndpoint()", text)
        self.assertIn("host = host || inferHost()", text)
        self.assertIn("port = port || inferPort()", text)


if __name__ == "__main__":
    unittest.main()
