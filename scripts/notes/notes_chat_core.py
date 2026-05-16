from __future__ import annotations

import re
import time
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import unquote, urlparse

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
NOTES_DIR = REPO_ROOT / "notes"
DEFAULT_MAX_DOC_CHARS = 60000
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
