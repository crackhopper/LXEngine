from __future__ import annotations

import pathlib
import tempfile
import unittest
from unittest import mock

from scripts.notes.notes_chat_core import (
    ChatError,
    ChatRequest,
    DocumentContext,
    build_agent_prompt,
    candidate_doc_paths,
    format_history,
    resolve_document,
)


class NotesChatCoreTest(unittest.TestCase):
    def test_candidate_doc_paths_keeps_index_and_readme_fallbacks(self) -> None:
        self.assertEqual(candidate_doc_paths("/concepts/material/"), [
            "concepts/material.md",
            "concepts/material/index.md",
            "concepts/material/README.md",
        ])
        self.assertEqual(candidate_doc_paths("README.md"), ["README.md"])
        self.assertEqual(candidate_doc_paths("concepts/scene/index.md"), [
            "concepts/scene/index.md",
            "concepts/scene/README.md",
        ])

    def test_resolve_document_rejects_hidden_and_non_markdown_paths(self) -> None:
        with self.assertRaises(ChatError):
            resolve_document("../AGENTS.md", 1000)
        with self.assertRaises(ChatError):
            resolve_document(".secret.md", 1000)
        with self.assertRaises(ChatError):
            resolve_document("assets/image.png", 1000)

    def test_resolve_document_loads_markdown_and_marks_truncation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            notes_dir = pathlib.Path(tmp) / "notes"
            notes_dir.mkdir()
            (notes_dir / "demo.md").write_text("# Demo\nabcdef", encoding="utf-8")
            with mock.patch("scripts.notes.notes_chat_core.NOTES_DIR", notes_dir):
                doc = resolve_document("demo.md", 8)
        self.assertEqual(doc.rel_path, "demo.md")
        self.assertEqual(doc.title, "Demo")
        self.assertTrue(doc.truncated)
        self.assertEqual(doc.text, "# Demo\na")

    def test_prompt_is_read_only_and_includes_context(self) -> None:
        request = ChatRequest(
            document=DocumentContext(
                rel_path="concepts/demo.md",
                title="Demo",
                text="# Demo\nBody",
                truncated=False,
            ),
            message="解释这一页",
            selected_text="Body",
            history=[{"role": "assistant", "text": "上一轮回答"}],
        )
        prompt = build_agent_prompt(request)
        self.assertIn("read-only documentation assistant", prompt)
        self.assertIn("Do not edit files", prompt)
        self.assertIn("notes/concepts/demo.md", prompt)
        self.assertIn("上一轮回答", prompt)
        self.assertIn("解释这一页", prompt)

    def test_format_history_skips_error_messages(self) -> None:
        text = format_history([
            {"role": "user", "text": "hello"},
            {"role": "error", "text": "hidden"},
            {"role": "assistant", "text": "world"},
        ])
        self.assertIn("user: hello", text)
        self.assertIn("assistant: world", text)
        self.assertNotIn("hidden", text)


if __name__ == "__main__":
    unittest.main()
