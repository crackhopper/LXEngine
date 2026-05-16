#!/usr/bin/env python3
"""Local read-only chat service for the notes MkDocs site."""

from __future__ import annotations

import argparse
import json
import os
import queue
import shlex
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import parse_qs, urlparse

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if __package__ in {None, ""}:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.notes.notes_chat_core import (
    DEFAULT_MAX_DOC_CHARS,
    ChatError,
    ChatRequest,
    build_agent_prompt,
    clean_text,
    first_param,
    resolve_document,
)
from scripts.notes.notes_chat_sessions import SESSION_DIR, SessionStore, session_summary

DEFAULT_TIMEOUT = 120.0


class AgentAdapter:
    name = "agent"

    def answer(self, request: ChatRequest) -> str:
        return "".join(self.stream(request)).strip()

    def stream(self, request: ChatRequest) -> Iterable[str]:
        raise NotImplementedError

    def health(self) -> dict[str, Any]:
        return {"name": self.name}


class ClaudeCliAdapter(AgentAdapter):
    name = "claude"

    def __init__(self, command: str, timeout: float) -> None:
        self.command = command
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        executable = shlex.split(self.command)[0] if self.command else "claude"
        return {
            "name": self.name,
            "transport": "claude-cli",
            "command": self.command,
            "available": shutil.which(executable) is not None,
            "streaming": True,
        }

    def stream(self, request: ChatRequest) -> Iterable[str]:
        base_cmd = shlex.split(self.command)
        if not base_cmd:
            raise ChatError(500, "Claude command is empty.")
        if shutil.which(base_cmd[0]) is None:
            raise ChatError(
                503,
                f"Claude command not found: {base_cmd[0]}. "
                "Install Claude Code or set NOTES_CHAT_CLAUDE_CMD.",
            )

        cmd = [
            *base_cmd,
            "-p",
            "--verbose",
            "--output-format",
            "stream-json",
            "--include-partial-messages",
            "--permission-mode",
            "plan",
            "--tools",
            "",
            "--no-session-persistence",
        ]
        prompt = build_agent_prompt(request)
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=REPO_ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
        except OSError as exc:
            raise ChatError(503, f"Failed to start Claude: {exc}") from exc

        assert proc.stdin is not None
        assert proc.stdout is not None
        proc.stdin.write(prompt)
        proc.stdin.close()

        previous_text = ""
        emitted_any = False
        started_at = time.monotonic()
        try:
            for line in proc.stdout:
                if time.monotonic() - started_at > self.timeout:
                    proc.kill()
                    raise ChatError(504, f"Claude request timed out after {self.timeout:g}s.")
                line = line.strip()
                if not line:
                    continue
                try:
                    payload = json.loads(line)
                except json.JSONDecodeError:
                    continue

                text = extract_claude_stream_text(payload)
                if text:
                    if text.startswith(previous_text):
                        delta = text[len(previous_text):]
                        previous_text = text
                    else:
                        delta = text
                        previous_text += text
                    if delta:
                        emitted_any = True
                        yield delta
                    continue

                result_text = extract_claude_result_text(payload)
                if result_text and not emitted_any:
                    emitted_any = True
                    previous_text = result_text
                    yield result_text

            return_code = proc.wait(timeout=5)
        except Exception:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)
            raise

        if return_code != 0:
            stderr = ""
            if proc.stderr is not None:
                stderr = proc.stderr.read().strip()
            if len(stderr) > 1200:
                stderr = stderr[:1200] + "\n..."
            raise ChatError(502, f"Claude exited with code {return_code}: {stderr}")


class AcpAdapter(AgentAdapter):
    name = "acp"

    def __init__(self, command: str, timeout: float) -> None:
        self.command = command
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        executable = shlex.split(self.command)[0] if self.command else ""
        return {
            "name": self.name,
            "transport": "acp-stdio",
            "command": self.command,
            "available": bool(executable and shutil.which(executable)),
            "streaming": True,
        }

    def stream(self, request: ChatRequest) -> Iterable[str]:
        if not self.command:
            raise ChatError(
                503,
                "ACP backend is enabled but no command is configured. "
                "Set NOTES_CHAT_ACP_CMD or pass --chat-agent-command.",
            )

        client = AcpStdioClient(shlex.split(self.command), self.timeout)
        try:
            client.start()
            client.request(
                "initialize",
                {
                    "protocolVersion": 1,
                    "clientCapabilities": {
                        "fs": {"readTextFile": False, "writeTextFile": False},
                        "terminal": False,
                    },
                    "clientInfo": {
                        "name": "notes-chat",
                        "title": "Notes Chat",
                        "version": "0.1.0",
                    },
                },
            )
            session = client.request("session/new", {"cwd": str(REPO_ROOT), "mcpServers": []})
            session_id = session.get("sessionId") if isinstance(session, dict) else None
            if not session_id:
                raise ChatError(502, "ACP agent did not return a sessionId.")
            prompt = build_agent_prompt(request)
            yield from client.stream_request(
                "session/prompt",
                {
                    "sessionId": session_id,
                    "prompt": [{"type": "text", "text": prompt}],
                },
            )
        finally:
            client.stop()


class AcpStdioClient:
    def __init__(self, command: list[str], timeout: float) -> None:
        self.command = command
        self.timeout = timeout
        self.proc: subprocess.Popen[str] | None = None
        self.messages: queue.Queue[dict[str, Any]] = queue.Queue()
        self.next_id = 1

    def start(self) -> None:
        if not self.command:
            raise ChatError(500, "ACP command is empty.")
        if shutil.which(self.command[0]) is None:
            raise ChatError(503, f"ACP command not found: {self.command[0]}.")
        self.proc = subprocess.Popen(
            self.command,
            cwd=REPO_ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        threading.Thread(target=self._read_stdout, daemon=True).start()

    def stop(self) -> None:
        proc = self.proc
        if proc is None:
            return
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)

    def request(self, method: str, params: dict[str, Any]) -> Any:
        request_id = self._send_request(method, params)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            message = self._next_message(deadline)
            if message is None:
                continue
            if "method" in message and "id" in message:
                self._reject_client_request(message)
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise_acp_error(message["error"])
            return message.get("result")
        raise ChatError(504, f"ACP request '{method}' timed out after {self.timeout:g}s.")

    def stream_request(self, method: str, params: dict[str, Any]) -> Iterable[str]:
        request_id = self._send_request(method, params)
        deadline = time.monotonic() + self.timeout
        emitted = False
        while time.monotonic() < deadline:
            message = self._next_message(deadline)
            if message is None:
                continue
            if "method" in message and "id" in message:
                self._reject_client_request(message)
                continue
            if message.get("method") == "session/update":
                text = extract_acp_update_text(message.get("params"))
                if text:
                    emitted = True
                    yield text
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise_acp_error(message["error"])
            if not emitted:
                result_text = extract_content_text(message.get("result"))
                if result_text:
                    yield result_text
            return
        raise ChatError(504, f"ACP request '{method}' timed out after {self.timeout:g}s.")

    def _send_request(self, method: str, params: dict[str, Any]) -> int:
        proc = self.proc
        if proc is None or proc.stdin is None:
            raise ChatError(500, "ACP process is not running.")
        request_id = self.next_id
        self.next_id += 1
        payload = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        }
        proc.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        proc.stdin.flush()
        return request_id

    def _next_message(self, deadline: float) -> dict[str, Any] | None:
        proc = self.proc
        if proc is None:
            raise ChatError(500, "ACP process is not running.")
        if proc.poll() is not None:
            stderr = self._read_stderr()
            raise ChatError(502, f"ACP agent exited early: {stderr}")
        remaining = max(0.1, deadline - time.monotonic())
        try:
            return self.messages.get(timeout=min(0.5, remaining))
        except queue.Empty:
            return None

    def _read_stdout(self) -> None:
        proc = self.proc
        if proc is None or proc.stdout is None:
            return
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                self.messages.put(json.loads(line))
            except json.JSONDecodeError:
                self.messages.put(
                    {
                        "jsonrpc": "2.0",
                        "error": {
                            "code": -32700,
                            "message": f"Invalid ACP JSON line: {line[:200]}",
                        },
                        "id": None,
                    }
                )

    def _read_stderr(self) -> str:
        proc = self.proc
        if proc is None or proc.stderr is None:
            return ""
        try:
            data = proc.stderr.read()
        except OSError:
            return ""
        return data.strip()[:1200]

    def _reject_client_request(self, message: dict[str, Any]) -> None:
        proc = self.proc
        if proc is None or proc.stdin is None:
            return
        response = {
            "jsonrpc": "2.0",
            "id": message.get("id"),
            "error": {
                "code": -32601,
                "message": "notes-chat is read-only and exposes no client methods",
            },
        }
        proc.stdin.write(json.dumps(response, separators=(",", ":")) + "\n")
        proc.stdin.flush()


def raise_acp_error(error: Any) -> None:
    if isinstance(error, dict):
        raise ChatError(502, f"ACP error: {error.get('message', error)}")
    raise ChatError(502, f"ACP error: {error}")


def extract_acp_update_text(params: Any) -> str:
    if not isinstance(params, dict):
        return ""
    update = params.get("update")
    if not isinstance(update, dict):
        return ""
    if update.get("sessionUpdate") != "agent_message_chunk":
        return ""
    return extract_content_text(update.get("content"))


def extract_content_text(content: Any) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "".join(extract_content_text(item) for item in content)
    if isinstance(content, dict):
        if isinstance(content.get("text"), str):
            return content["text"]
        if isinstance(content.get("content"), str):
            return content["content"]
        if isinstance(content.get("delta"), str):
            return content["delta"]
        if isinstance(content.get("message"), dict):
            return extract_content_text(content["message"])
        if isinstance(content.get("resource"), dict):
            return extract_content_text(content["resource"])
        if isinstance(content.get("content"), list):
            return extract_content_text(content["content"])
    return ""


def extract_claude_stream_text(payload: dict[str, Any]) -> str:
    if payload.get("type") == "stream_event" and isinstance(payload.get("event"), dict):
        event = payload["event"]
        if event.get("type") == "content_block_delta":
            return extract_content_text(event.get("delta"))
        if event.get("type") == "content_block_start":
            return extract_content_text(event.get("content_block"))
    if payload.get("type") == "assistant" and isinstance(payload.get("message"), dict):
        return extract_content_text(payload["message"].get("content"))
    if payload.get("type") in {"assistant_delta", "content_block_delta"}:
        return extract_content_text(payload)
    return ""


def extract_claude_result_text(payload: dict[str, Any]) -> str:
    if payload.get("type") == "result" and isinstance(payload.get("result"), str):
        return payload["result"]
    if isinstance(payload.get("result"), dict):
        return extract_content_text(payload["result"])
    return ""


def make_adapter(agent: str, command: str, timeout: float) -> AgentAdapter:
    if agent == "claude":
        return ClaudeCliAdapter(command or os.environ.get("NOTES_CHAT_CLAUDE_CMD", "claude"), timeout)
    if agent == "acp":
        return AcpAdapter(command or os.environ.get("NOTES_CHAT_ACP_CMD", ""), timeout)
    raise ChatError(500, f"Unsupported agent backend: {agent}")


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
    parser.add_argument("--agent", choices=["claude", "acp"], default=os.environ.get("NOTES_CHAT_AGENT", "claude"))
    parser.add_argument("--agent-command", default=os.environ.get("NOTES_CHAT_AGENT_COMMAND", ""))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--max-doc-chars", type=int, default=DEFAULT_MAX_DOC_CHARS)
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.max_doc_chars <= 0:
        parser.error("--max-doc-chars must be positive")

    adapter = make_adapter(args.agent, args.agent_command, args.timeout)
    store = SessionStore(SESSION_DIR)
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
