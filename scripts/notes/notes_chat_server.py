#!/usr/bin/env python3
"""Local read-only chat service for the notes MkDocs site."""

from __future__ import annotations

import argparse
import json
import os
import queue
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from typing import Any, Iterable
from urllib.parse import parse_qs, unquote, urlparse

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
NOTES_DIR = REPO_ROOT / "notes"
SESSION_DIR = REPO_ROOT / ".tmp" / "notes-chat" / "sessions"
DEFAULT_MAX_DOC_CHARS = 60000
DEFAULT_TIMEOUT = 120.0
MAX_HISTORY_MESSAGES = 24
MAX_HISTORY_CHARS = 24000
SESSION_ID_RE = re.compile(r"^[A-Za-z0-9_-]{8,80}$")


class ChatError(Exception):
    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


@dataclass(frozen=True)
class DocumentContext:
    rel_path: str
    title: str
    text: str
    truncated: bool


@dataclass(frozen=True)
class ChatRequest:
    document: DocumentContext
    message: str
    selected_text: str
    history: list[dict[str, Any]]


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


class SessionStore:
    def __init__(self, root: Path) -> None:
        self.root = root
        self._lock = threading.RLock()

    def create(self, *, title: str | None = None, doc_path: str = "") -> dict[str, Any]:
        now = now_iso()
        session = {
            "id": uuid.uuid4().hex,
            "title": clean_session_title(title) or "新会话",
            "createdAt": now,
            "updatedAt": now,
            "lastDocPath": doc_path,
            "messages": [],
        }
        with self._lock:
            self._write(session)
        return session

    def list(self) -> list[dict[str, Any]]:
        with self._lock:
            sessions = [self._read_path(path) for path in self._iter_paths()]
        items = [session_summary(session) for session in sessions if session]
        items.sort(key=lambda item: item.get("updatedAt", ""), reverse=True)
        return items

    def get(self, session_id: str) -> dict[str, Any]:
        validate_session_id(session_id)
        with self._lock:
            path = self._path(session_id)
            if not path.is_file():
                raise ChatError(404, f"Session not found: {session_id}")
            session = self._read_path(path)
            if not session:
                raise ChatError(404, f"Session not found: {session_id}")
            return session

    def delete(self, session_id: str) -> None:
        validate_session_id(session_id)
        with self._lock:
            path = self._path(session_id)
            if path.is_file():
                path.unlink()
            else:
                raise ChatError(404, f"Session not found: {session_id}")

    def append_message(
        self,
        session_id: str,
        *,
        role: str,
        text: str,
        doc_path: str = "",
        status: str = "done",
    ) -> dict[str, Any]:
        with self._lock:
            session = self.get(session_id)
            message = {
                "id": uuid.uuid4().hex,
                "role": role,
                "text": text,
                "docPath": doc_path,
                "status": status,
                "createdAt": now_iso(),
            }
            session.setdefault("messages", []).append(message)
            if doc_path:
                session["lastDocPath"] = doc_path
            if session.get("title") == "新会话" and role == "user":
                session["title"] = clean_session_title(text) or session["title"]
            session["updatedAt"] = now_iso()
            self._write(session)
            return message

    def update_last_doc(self, session_id: str, doc_path: str) -> dict[str, Any]:
        with self._lock:
            session = self.get(session_id)
            session["lastDocPath"] = doc_path
            session["updatedAt"] = now_iso()
            self._write(session)
            return session

    def _iter_paths(self) -> list[Path]:
        if not self.root.is_dir():
            return []
        return sorted(self.root.glob("*.json"))

    def _path(self, session_id: str) -> Path:
        return self.root / f"{session_id}.json"

    def _read_path(self, path: Path) -> dict[str, Any] | None:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        if not isinstance(data, dict) or not isinstance(data.get("id"), str):
            return None
        data.setdefault("messages", [])
        return data

    def _write(self, session: dict[str, Any]) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        path = self._path(str(session["id"]))
        tmp_path = path.with_suffix(".json.tmp")
        tmp_path.write_text(
            json.dumps(session, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        tmp_path.replace(path)


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


def build_agent_prompt(request: ChatRequest) -> str:
    doc = request.document
    selected = request.selected_text.strip()
    selected_block = ""
    if selected:
        selected_block = (
            "\n\nSelected text from the current page:\n"
            "```text\n"
            f"{selected[:12000]}\n"
            "```"
        )
    truncated_note = (
        "\n\nNote: the document content was truncated by the local chat service."
        if doc.truncated
        else ""
    )
    history_block = format_history(request.history)
    return (
        "You are a read-only documentation assistant for a local MkDocs notes site.\n"
        "Rules:\n"
        "- Answer directly and concretely. Do not refer to an answer that has not been given.\n"
        "- Use the current document and the conversation history below.\n"
        "- Do not edit files, do not run commands, and do not claim that you changed the project.\n"
        "- If the user asks you to modify files, explain that this web chat is read-only and "
        "suggest using the terminal agent workflow.\n"
        "- Match the user's language.\n\n"
        f"{history_block}"
        f"Current document: notes/{doc.rel_path}\n"
        f"Document title: {doc.title}\n"
        f"{truncated_note}\n\n"
        "Current document Markdown:\n"
        "```markdown\n"
        f"{doc.text}\n"
        "```"
        f"{selected_block}\n\n"
        "Current user question:\n"
        f"{request.message.strip()}\n"
    )


def format_history(messages: list[dict[str, Any]]) -> str:
    if not messages:
        return "Conversation history: (new session)\n\n"
    selected = messages[-MAX_HISTORY_MESSAGES:]
    lines = ["Conversation history:"]
    used = 0
    for message in selected:
        role = str(message.get("role") or "message")
        if role == "error":
            continue
        text = str(message.get("text") or "").strip()
        if not text:
            continue
        remaining = MAX_HISTORY_CHARS - used
        if remaining <= 0:
            break
        text = text[:remaining]
        used += len(text)
        lines.append(f"{role}: {text}")
    lines.append("")
    return "\n".join(lines) + "\n"


def extract_title(text: str, fallback: str) -> str:
    for line in text.splitlines():
        if line.startswith("# "):
            return line[2:].strip() or fallback
    return fallback


def resolve_document(raw_path: str, max_doc_chars: int) -> DocumentContext:
    candidates = candidate_doc_paths(raw_path)
    tried: list[str] = []
    for candidate in candidates:
        path = validate_note_path(candidate)
        tried.append(candidate)
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            truncated = len(text) > max_doc_chars
            if truncated:
                text = text[:max_doc_chars]
            rel = path.relative_to(NOTES_DIR).as_posix()
            return DocumentContext(
                rel_path=rel,
                title=extract_title(text, Path(rel).stem),
                text=text,
                truncated=truncated,
            )
    raise ChatError(404, f"Notes document not found. Tried: {', '.join(tried)}")


def candidate_doc_paths(raw_path: str) -> list[str]:
    if not raw_path:
        return ["README.md", "index.md"]

    parsed = urlparse(raw_path)
    path = parsed.path if parsed.scheme or parsed.netloc else raw_path
    path = unquote(path).replace("\\", "/").split("?", 1)[0].split("#", 1)[0]
    path = path.strip()
    while path.startswith("/"):
        path = path[1:]
    while path.endswith("/"):
        path = path[:-1]

    if not path:
        return ["README.md", "index.md"]
    if path.endswith(".md"):
        if path == "README.md":
            return [path]
        if path == "index.md":
            return [path, "README.md"]
        if path.endswith("/README.md"):
            return [path]
        if path.endswith("/index.md"):
            return [path, f"{path[:-9]}/README.md"]
        stem = path[:-3]
        return [path, f"{stem}/index.md", f"{stem}/README.md"]
    return [f"{path}.md", f"{path}/index.md", f"{path}/README.md"]


def validate_note_path(rel_path: str) -> Path:
    pure = PurePosixPath(rel_path)
    if pure.is_absolute() or any(part in {"", ".", ".."} for part in pure.parts):
        raise ChatError(400, f"Invalid notes path: {rel_path}")
    if any(part.startswith(".") for part in pure.parts):
        raise ChatError(400, f"Hidden notes paths are not allowed: {rel_path}")
    if pure.suffix != ".md":
        raise ChatError(400, f"Only Markdown notes can be read: {rel_path}")
    path = (NOTES_DIR / Path(*pure.parts)).resolve()
    notes_root = NOTES_DIR.resolve()
    try:
        path.relative_to(notes_root)
    except ValueError as exc:
        raise ChatError(400, f"Path escapes notes/: {rel_path}") from exc
    return path


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
            self.server.store.delete(session_id)
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


def session_summary(session: dict[str, Any]) -> dict[str, Any]:
    messages = session.get("messages") or []
    return {
        "id": session.get("id"),
        "title": session.get("title") or "新会话",
        "createdAt": session.get("createdAt"),
        "updatedAt": session.get("updatedAt"),
        "lastDocPath": session.get("lastDocPath") or "",
        "messageCount": len(messages),
    }


def validate_session_id(session_id: str) -> None:
    if not SESSION_ID_RE.match(session_id):
        raise ChatError(400, f"Invalid session id: {session_id}")


def clean_session_title(title: str | None) -> str:
    if not title:
        return ""
    cleaned = " ".join(title.strip().split())
    if len(cleaned) > 48:
        cleaned = cleaned[:48].rstrip() + "..."
    return cleaned


def first_param(params: dict[str, list[str]], name: str) -> str:
    values = params.get(name) or []
    return values[0] if values else ""


def clean_text(value: Any, name: str, limit: int) -> str:
    if not isinstance(value, str):
        raise ChatError(400, f"{name} must be a string.")
    text = value.strip()
    if not text and name == "message":
        raise ChatError(400, "message is required.")
    if len(text) > limit:
        raise ChatError(413, f"{name} is too long.")
    return text


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


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
