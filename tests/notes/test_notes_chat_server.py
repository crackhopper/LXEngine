from __future__ import annotations

import io
import json
import pathlib
import sys
import tempfile
import threading
import unittest
from unittest import mock
from urllib import request as urlrequest

from scripts.notes import notes_chat_server
from scripts.notes.notes_chat_agents import AgentAdapter
from scripts.notes.notes_chat_sessions import SessionStore


class FakeCodexAdapter(AgentAdapter):
    name = "codex"

    def __init__(self) -> None:
        self.requests = []

    def stream(self, request):
        self.requests.append(request)
        yield "fake "
        yield "answer"

    def health(self):
        return {
            "name": self.name,
            "transport": "mcp-stdio",
            "command": "codex mcp-server",
            "available": False,
            "streaming": True,
        }


class NotesChatServerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.tmp_path = pathlib.Path(self.tmp.name)
        self.notes_dir = self.tmp_path / "notes"
        self.notes_dir.mkdir()
        self.sessions_dir = self.tmp_path / "sessions"
        self.notes_patch = mock.patch("scripts.notes.notes_chat_core.NOTES_DIR", self.notes_dir)
        self.notes_patch.start()
        self.adapter = FakeCodexAdapter()
        self.store = SessionStore(self.sessions_dir)
        self.server = notes_chat_server.NotesChatServer(
            ("127.0.0.1", 0),
            self.adapter,
            self.store,
            1000,
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        host, port = self.server.server_address
        self.base_url = f"http://{host}:{port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.thread.join(timeout=5)
        self.server.server_close()
        self.notes_patch.stop()
        self.tmp.cleanup()

    def get_json(self, path: str) -> dict:
        with urlrequest.urlopen(f"{self.base_url}{path}", timeout=5) as response:
            self.assertEqual(response.status, 200)
            return json.loads(response.read().decode("utf-8"))

    def post_json(self, path: str, payload: dict) -> tuple[dict, str]:
        body = json.dumps(payload).encode("utf-8")
        req = urlrequest.Request(
            f"{self.base_url}{path}",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urlrequest.urlopen(req, timeout=5) as response:
            self.assertEqual(response.status, 200)
            text = response.read().decode("utf-8")
            content_type = response.headers.get("Content-Type", "")
        return json.loads(text), content_type

    def post_text(self, path: str, payload: dict) -> tuple[str, str]:
        body = json.dumps(payload).encode("utf-8")
        req = urlrequest.Request(
            f"{self.base_url}{path}",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urlrequest.urlopen(req, timeout=5) as response:
            self.assertEqual(response.status, 200)
            text = response.read().decode("utf-8")
            content_type = response.headers.get("Content-Type", "")
        return text, content_type

    def test_invalid_agent_env_default_reports_argparse_error(self) -> None:
        stderr = io.StringIO()
        with (
            mock.patch.dict("os.environ", {"NOTES_CHAT_AGENT": "bad"}, clear=True),
            mock.patch.object(sys, "argv", ["notes_chat_server.py", "--port", "0"]),
            mock.patch("sys.stderr", stderr),
        ):
            with self.assertRaises(SystemExit) as raised:
                notes_chat_server.main()

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("argument --agent: invalid choice: 'bad'", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())

    def test_health_reports_read_only_codex_style_adapter(self) -> None:
        payload = self.get_json("/health")

        self.assertEqual(
            payload,
            {
                "ok": True,
                "readOnly": True,
                "agent": {
                    "name": "codex",
                    "transport": "mcp-stdio",
                    "command": "codex mcp-server",
                    "available": False,
                    "streaming": True,
                },
            },
        )

    def test_chat_json_resolves_document_appends_session_and_wires_adapter(self) -> None:
        (self.notes_dir / "demo.md").write_text("# Demo\nBody", encoding="utf-8")

        payload, content_type = self.post_json(
            "/chat",
            {
                "docPath": "demo",
                "message": "Explain this page",
                "selectedText": "Body",
            },
        )

        self.assertIn("application/json", content_type)
        self.assertEqual(payload["answer"], "fake answer")
        self.assertEqual(payload["agent"], "codex")
        self.assertEqual(payload["docPath"], "demo.md")
        self.assertTrue(payload["readOnly"])
        self.assertEqual(payload["session"]["messageCount"], 2)
        self.assertEqual([message["role"] for message in payload["messages"]], ["user", "assistant"])
        self.assertEqual(len(self.adapter.requests), 1)
        adapter_request = self.adapter.requests[0]
        self.assertEqual(adapter_request.document.rel_path, "demo.md")
        self.assertEqual(adapter_request.document.title, "Demo")
        self.assertEqual(adapter_request.message, "Explain this page")
        self.assertEqual(adapter_request.selected_text, "Body")
        stored = self.store.get(payload["session"]["id"])
        self.assertEqual([message["text"] for message in stored["messages"]], ["Explain this page", "fake answer"])
        self.assertEqual(len(list(self.sessions_dir.glob("*.json"))), 1)

    def test_chat_stream_emits_sse_text_and_persists_answer(self) -> None:
        (self.notes_dir / "stream.md").write_text("# Stream\nBody", encoding="utf-8")

        text, content_type = self.post_text(
            "/chat/stream",
            {
                "docPath": "stream.md",
                "message": "Stream this page",
            },
        )

        self.assertIn("text/event-stream", content_type)
        self.assertIn("event: session\n", text)
        self.assertIn('event: delta\ndata: {"text": "fake "}', text)
        self.assertIn('event: delta\ndata: {"text": "answer"}', text)
        self.assertIn("event: done\n", text)
        sessions = self.store.list()
        self.assertEqual(len(sessions), 1)
        stored = self.store.get(sessions[0]["id"])
        self.assertEqual([message["role"] for message in stored["messages"]], ["user", "assistant"])
        self.assertEqual(stored["messages"][1]["text"], "fake answer")
        self.assertEqual(stored["lastDocPath"], "stream.md")


if __name__ == "__main__":
    unittest.main()
