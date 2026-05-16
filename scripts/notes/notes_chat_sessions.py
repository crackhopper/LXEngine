from __future__ import annotations

import json
import threading
import uuid
from pathlib import Path
from typing import Any

from scripts.notes.notes_chat_core import (
    ChatError,
    REPO_ROOT,
    clean_session_title,
    now_iso,
    validate_session_id,
)

SESSION_DIR = REPO_ROOT / ".tmp" / "notes-chat" / "sessions"


class SessionStore:
    def __init__(self, root: Path | None = None) -> None:
        self.root = root if root is not None else SESSION_DIR
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

    def delete(self, session_id: str) -> bool:
        validate_session_id(session_id)
        with self._lock:
            path = self._path(session_id)
            if path.is_file():
                path.unlink()
                return True
            return False

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
