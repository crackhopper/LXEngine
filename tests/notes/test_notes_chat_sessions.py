from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.notes.notes_chat_core import ChatError, REPO_ROOT
from scripts.notes.notes_chat_sessions import SessionStore, session_summary
from scripts.notes import notes_chat_sessions


class NotesChatSessionsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="notes-chat-sessions-"))
        self.session_dir_patch = mock.patch("scripts.notes.notes_chat_sessions.SESSION_DIR", self.root)
        self.session_dir_patch.start()
        self.store = SessionStore()

    def tearDown(self) -> None:
        self.session_dir_patch.stop()
        shutil.rmtree(self.root, ignore_errors=True)

    def test_create_append_read_and_summary_cleans_title(self) -> None:
        long_title = "  Hello   world  " + ("x" * 80)
        session = self.store.create(title=long_title, doc_path="README.md")

        self.assertEqual(session["title"], "Hello world " + ("x" * 36) + "...")
        self.assertEqual(session["lastDocPath"], "README.md")

        user = self.store.append_message(
            session["id"],
            role="user",
            text="Question",
            doc_path="README.md",
        )
        assistant = self.store.append_message(
            session["id"],
            role="assistant",
            text="Answer",
            doc_path="concepts/demo.md",
        )
        updated = self.store.get(session["id"])

        self.assertEqual([user["role"], assistant["role"]], ["user", "assistant"])
        self.assertEqual([message["text"] for message in updated["messages"]], ["Question", "Answer"])
        self.assertEqual(updated["lastDocPath"], "concepts/demo.md")
        summary = session_summary(updated)
        self.assertEqual(summary["messageCount"], 2)
        self.assertEqual(summary["title"], session["title"])

    def test_invalid_session_ids_are_rejected(self) -> None:
        for operation in (
            lambda: self.store.get("../bad"),
            lambda: self.store.append_message("../bad", role="user", text="Question"),
            lambda: self.store.update_last_doc("../bad", "README.md"),
            lambda: self.store.delete("../bad"),
        ):
            with self.subTest(operation=operation):
                with self.assertRaises(ChatError):
                    operation()

    def test_delete_missing_session_is_false(self) -> None:
        self.assertFalse(self.store.delete("missing123"))


class NotesChatSessionDefaultsTest(unittest.TestCase):
    def test_default_session_dir_preserves_tmp_location(self) -> None:
        self.assertEqual(
            notes_chat_sessions.SESSION_DIR,
            REPO_ROOT / ".tmp" / "notes-chat" / "sessions",
        )


if __name__ == "__main__":
    unittest.main()
