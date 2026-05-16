#!/usr/bin/env python3
"""Local read-only chat service for the notes MkDocs site."""

from __future__ import annotations

import argparse
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if __package__ in {None, ""}:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.notes.notes_chat_core import (
    DEFAULT_MAX_DOC_CHARS,
    ChatError,
    ChatRequest,
    clean_text,
    first_param,
    resolve_document,
)
from scripts.notes.notes_chat_agents import AgentAdapter, make_adapter
from scripts.notes.notes_chat_sessions import SessionStore, session_summary

DEFAULT_TIMEOUT = 120.0
SUPPORTED_AGENTS = ["codex", "codex-exec", "claude", "acp"]


def supported_agent_choices() -> str:
    return ", ".join(repr(agent) for agent in SUPPORTED_AGENTS)


class NotesChatHandler(BaseHTTPRequestHandler):
    server: "NotesChatServer"

    def do_OPTIONS(self) -> None:  # noqa: N802 - stdlib hook name
        self.send_response(204)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802 - stdlib hook name
        try:
            parsed = urlparse(self.path)
            if parsed.path == "/health":
                self.send_json(
                    200,
                    {
                        "ok": True,
                        "readOnly": True,
                        "agent": self.server.adapter.health(),
                    },
                )
                return
            if parsed.path == "/sessions":
                self.send_json(200, {"sessions": self.server.store.list()})
                return
            if parsed.path.startswith("/sessions/"):
                session_id = parsed.path.rsplit("/", 1)[-1]
                self.send_json(200, {"session": self.server.store.get(session_id)})
                return
            if parsed.path == "/doc":
                params = parse_qs(parsed.query)
                raw_path = first_param(params, "path") or first_param(params, "pagePath")
                doc = resolve_document(raw_path, self.server.max_doc_chars)
                self.send_json(
                    200,
                    {
                        "path": doc.rel_path,
                        "title": doc.title,
                        "truncated": doc.truncated,
                    },
                )
                return
            self.send_json(404, {"error": "not found"})
        except ChatError as exc:
            self.send_json(exc.status, {"error": exc.message})
        except Exception as exc:  # pragma: no cover - defensive server boundary
            self.send_json(500, {"error": str(exc)})

    def do_DELETE(self) -> None:  # noqa: N802 - stdlib hook name
        try:
            parsed = urlparse(self.path)
            if not parsed.path.startswith("/sessions/"):
                self.send_json(404, {"error": "not found"})
                return
            session_id = parsed.path.rsplit("/", 1)[-1]
            if not self.server.store.delete(session_id):
                raise ChatError(404, f"Session not found: {session_id}")
            self.send_json(200, {"ok": True})
        except ChatError as exc:
            self.send_json(exc.status, {"error": exc.message})
        except Exception as exc:  # pragma: no cover - defensive server boundary
            self.send_json(500, {"error": str(exc)})

    def do_POST(self) -> None:  # noqa: N802 - stdlib hook name
        try:
            parsed = urlparse(self.path)
            if parsed.path == "/sessions":
                payload = self.read_json_body(required=False)
                session = self.server.store.create(
                    title=str(payload.get("title") or "") if payload else None,
                    doc_path=str(payload.get("docPath") or "") if payload else "",
                )
                self.send_json(200, {"session": session_summary(session)})
                return
            if parsed.path == "/chat":
                self.handle_chat_json()
                return
            if parsed.path == "/chat/stream":
                self.handle_chat_stream()
                return
            self.send_json(404, {"error": "not found"})
        except ChatError as exc:
            self.send_json(exc.status, {"error": exc.message})
        except Exception as exc:  # pragma: no cover - defensive server boundary
            self.send_json(500, {"error": str(exc)})

    def handle_chat_json(self) -> None:
        payload = self.read_json_body()
        request, session = self.prepare_chat_request(payload)
        user = self.server.store.append_message(
            session["id"],
            role="user",
            text=request.message,
            doc_path=request.document.rel_path,
        )
        answer = self.server.adapter.answer(request)
        assistant = self.server.store.append_message(
            session["id"],
            role="assistant",
            text=answer,
            doc_path=request.document.rel_path,
        )
        updated = self.server.store.get(session["id"])
        self.send_json(
            200,
            {
                "answer": answer,
                "agent": self.server.adapter.name,
                "docPath": request.document.rel_path,
                "readOnly": True,
                "session": session_summary(updated),
                "messages": [user, assistant],
            },
        )

    def handle_chat_stream(self) -> None:
        payload = self.read_json_body()
        request, session = self.prepare_chat_request(payload)
        self.server.store.append_message(
            session["id"],
            role="user",
            text=request.message,
            doc_path=request.document.rel_path,
        )
        self.send_sse_headers()
        self.send_sse("session", {"session": session_summary(self.server.store.get(session["id"]))})

        chunks: list[str] = []
        try:
            for chunk in self.server.adapter.stream(request):
                if not chunk:
                    continue
                chunks.append(chunk)
                self.send_sse("delta", {"text": chunk})
            answer = "".join(chunks).strip()
            assistant = self.server.store.append_message(
                session["id"],
                role="assistant",
                text=answer,
                doc_path=request.document.rel_path,
            )
            updated = self.server.store.get(session["id"])
            self.send_sse(
                "done",
                {
                    "message": assistant,
                    "session": session_summary(updated),
                },
            )
            self.close_connection = True
        except ChatError as exc:
            self.server.store.append_message(
                session["id"],
                role="error",
                text=exc.message,
                doc_path=request.document.rel_path,
                status="error",
            )
            self.send_sse("error", {"error": exc.message, "status": exc.status})
            self.close_connection = True
        except Exception as exc:  # pragma: no cover - defensive server boundary
            self.server.store.append_message(
                session["id"],
                role="error",
                text=str(exc),
                doc_path=request.document.rel_path,
                status="error",
            )
            self.send_sse("error", {"error": str(exc), "status": 500})
            self.close_connection = True

    def prepare_chat_request(self, payload: dict[str, Any]) -> tuple[ChatRequest, dict[str, Any]]:
        message = clean_text(payload.get("message"), "message", limit=20000)
        selected_text = clean_text(payload.get("selectedText", ""), "selectedText", limit=20000)
        raw_path = str(payload.get("docPath") or payload.get("pagePath") or "")
        doc = resolve_document(raw_path, self.server.max_doc_chars)
        session_id = str(payload.get("sessionId") or "").strip()
        if session_id:
            session = self.server.store.update_last_doc(session_id, doc.rel_path)
        else:
            session = self.server.store.create(title=message, doc_path=doc.rel_path)
        history = list(session.get("messages") or [])
        return (
            ChatRequest(
                document=doc,
                message=message,
                selected_text=selected_text,
                history=history,
            ),
            session,
        )

    def read_json_body(self, required: bool = True) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0:
            if required:
                raise ChatError(400, "Missing JSON request body.")
            return {}
        if length > 256000:
            raise ChatError(413, "Request body is too large.")
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ChatError(400, "Invalid JSON request body.") from exc
        if not isinstance(payload, dict):
            raise ChatError(400, "JSON request body must be an object.")
        return payload

    def send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_cors_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_sse_headers(self) -> None:
        self.send_response(200)
        self.send_cors_headers()
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()

    def send_sse(self, event: str, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False)
        data = f"event: {event}\ndata: {body}\n\n".encode("utf-8")
        try:
            self.wfile.write(data)
            self.wfile.flush()
        except BrokenPipeError:
            raise ChatError(499, "Client disconnected.")

    def send_cors_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def log_message(self, fmt: str, *args: object) -> None:
        return


class NotesChatServer(ThreadingHTTPServer):
    def __init__(
        self,
        server_address: tuple[str, int],
        adapter: AgentAdapter,
        store: SessionStore,
        max_doc_chars: int,
    ) -> None:
        super().__init__(server_address, NotesChatHandler)
        self.adapter = adapter
        self.store = store
        self.max_doc_chars = max_doc_chars


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--agent", choices=SUPPORTED_AGENTS, default=os.environ.get("NOTES_CHAT_AGENT", "codex"))
    parser.add_argument("--agent-command", default=os.environ.get("NOTES_CHAT_AGENT_COMMAND", ""))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--max-doc-chars", type=int, default=DEFAULT_MAX_DOC_CHARS)
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.max_doc_chars <= 0:
        parser.error("--max-doc-chars must be positive")
    if args.agent not in SUPPORTED_AGENTS:
        parser.error(
            f"argument --agent: invalid choice: {args.agent!r} "
            f"(choose from {supported_agent_choices()})"
        )

    adapter = make_adapter(args.agent, args.agent_command, args.timeout)
    store = SessionStore()
    server = NotesChatServer((args.host, args.port), adapter, store, args.max_doc_chars)
    print(
        f">> notes chat endpoint: http://{args.host}:{args.port} "
        f"(agent={adapter.name}, read-only)",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
