from __future__ import annotations

import pathlib
import re
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SUPPORTED_AGENTS = {"codex", "claude", "acp"}


class NotesServeScriptsTest(unittest.TestCase):
    def test_shell_script_defaults_to_codex_and_accepts_supported_agents(self) -> None:
        text = (REPO_ROOT / "scripts/notes/serve_site.sh").read_text(encoding="utf-8")
        self.assertIn('CHAT_AGENT="${NOTES_CHAT_AGENT:-codex}"', text)
        self.assertIn('--chat-agent must be codex, claude, or acp', text)
        self.assertNotIn("must be claude or acp", text)
        self.assertNotIn('"codex-exec"', text)
        for agent in SUPPORTED_AGENTS:
            self.assertIn(f'"{agent}"', text)

    def test_powershell_script_defaults_to_codex_and_accepts_supported_agents(self) -> None:
        text = (REPO_ROOT / "scripts/notes/serve_site.ps1").read_text(encoding="utf-8")
        self.assertIn('else { "codex" }', text)
        validate_set = re.search(r'\[ValidateSet\(([^\)]*)\)\]', text)
        self.assertIsNotNone(validate_set)
        values = validate_set.group(1)
        self.assertNotIn('"codex-exec"', values)
        for agent in SUPPORTED_AGENTS:
            self.assertIn(f'"{agent}"', values)


if __name__ == "__main__":
    unittest.main()
