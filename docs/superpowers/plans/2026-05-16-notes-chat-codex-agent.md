# Notes Chat Codex Agent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the notes web chat use Codex by default through a shared agent/protocol abstraction while preserving Claude and ACP support.

**Architecture:** Split the current monolithic `notes_chat_server.py` into core, session, agent-binding, protocol, and HTTP server modules. Add a default `codex` binding backed by a stdio MCP client for `codex mcp-server`, plus a `codex-exec` fallback backed by `codex exec --json`. Keep the public `/chat` and `/chat/stream` API unchanged.

**Tech Stack:** Python 3 stdlib `unittest`, stdio JSON-RPC/MCP, subprocess supervision, Bash, PowerShell, MkDocs notes tooling.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `scripts/notes/notes_chat_core.py` | Create | Notes-specific dataclasses, document resolution, prompt construction, history formatting, request validation helpers. |
| `scripts/notes/notes_chat_sessions.py` | Create | Session JSON persistence under `.tmp/notes-chat/sessions/`. |
| `scripts/notes/notes_chat_agents.py` | Create | `AgentAdapter`, agent names, command defaults, `make_adapter(...)`. |
| `scripts/notes/notes_chat_protocols.py` | Create | Claude CLI, ACP stdio, MCP stdio, and CLI JSONL protocol implementations. |
| `scripts/notes/notes_chat_server.py` | Modify | Keep HTTP/SSE routes and startup only; import core/session/agent modules. |
| `scripts/notes/serve_site.sh` | Modify | Default chat agent to `codex`; accept `codex`, `codex-exec`, `claude`, `acp`. |
| `scripts/notes/serve_site.ps1` | Modify | Same defaults and validation as shell script. |
| `scripts/notes/watch_site_inputs.py` | Modify | Pass new agent names through unchanged. |
| `notes/tools/notes-tooling.md` | Modify | Document Codex default, Claude/ACP overrides, and fallback. |
| `tests/notes/__init__.py` | Create | Python package marker. |
| `tests/notes/fakes/fake_mcp_server.py` | Create | Fake stdio MCP server for deterministic tests. |
| `tests/notes/fakes/fake_jsonl_agent.py` | Create | Fake JSONL CLI process for `codex-exec` tests. |
| `tests/notes/test_notes_chat_core.py` | Create | Core prompt/document/history tests. |
| `tests/notes/test_notes_chat_sessions.py` | Create | Session persistence tests. |
| `tests/notes/test_notes_chat_agents.py` | Create | Agent binding and health tests. |
| `tests/notes/test_notes_chat_protocols.py` | Create | MCP, CLI JSONL, Claude extraction, ACP extraction tests. |
| `tests/notes/test_notes_chat_server.py` | Create | HTTP and SSE compatibility tests with fake adapter. |
| `tests/notes/test_notes_serve_scripts.py` | Create | Shell/PowerShell default and validation text tests. |

## Task 1: Extract Notes Chat Core Without Behavior Change

**Files:**
- Create: `tests/notes/__init__.py`
- Create: `tests/notes/test_notes_chat_core.py`
- Create: `scripts/notes/notes_chat_core.py`
- Modify: `scripts/notes/notes_chat_server.py`

- [ ] **Step 1: Write core extraction tests**

Create `tests/notes/__init__.py` as an empty file.

Create `tests/notes/test_notes_chat_core.py`:

```python
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
```

- [ ] **Step 2: Run the failing core tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_core
```

Expected: import failure for `scripts.notes.notes_chat_core`.

- [ ] **Step 3: Move core code into `notes_chat_core.py`**

Create `scripts/notes/notes_chat_core.py` by moving these definitions out of `notes_chat_server.py` without changing behavior:

```python
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
```

Then move the existing implementations of:

```python
build_agent_prompt
format_history
extract_title
resolve_document
candidate_doc_paths
validate_note_path
validate_session_id
clean_session_title
first_param
clean_text
now_iso
```

Keep function signatures identical.

- [ ] **Step 4: Update `notes_chat_server.py` imports**

Remove the moved definitions from `notes_chat_server.py` and import them:

```python
from scripts.notes.notes_chat_core import (
    DEFAULT_MAX_DOC_CHARS,
    ChatError,
    ChatRequest,
    clean_text,
    first_param,
    resolve_document,
)
from scripts.notes.notes_chat_core import clean_session_title, now_iso, validate_session_id
```

At this stage `SessionStore` may still live in `notes_chat_server.py`; it will be moved in Task 2.

- [ ] **Step 5: Run core tests and smoke server compile**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_core
python3 -m py_compile scripts/notes/notes_chat_server.py scripts/notes/notes_chat_core.py
```

Expected: tests pass; compile succeeds.

- [ ] **Step 6: Commit Task 1**

```bash
git add scripts/notes/notes_chat_server.py scripts/notes/notes_chat_core.py tests/notes/__init__.py tests/notes/test_notes_chat_core.py
git commit -m "Extract notes chat core helpers"
```

## Task 2: Extract Session Store

**Files:**
- Create: `tests/notes/test_notes_chat_sessions.py`
- Create: `scripts/notes/notes_chat_sessions.py`
- Modify: `scripts/notes/notes_chat_server.py`

- [ ] **Step 1: Write session tests**

Create `tests/notes/test_notes_chat_sessions.py`:

```python
from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path

from scripts.notes.notes_chat_core import ChatError
from scripts.notes.notes_chat_sessions import SessionStore, session_summary


class NotesChatSessionsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="notes-chat-sessions-"))
        self.store = SessionStore(self.root)

    def tearDown(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)

    def test_create_append_and_summary(self) -> None:
        session = self.store.create(title="Hello world", doc_path="README.md")
        self.assertEqual(session["title"], "Hello world")
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
            doc_path="README.md",
        )
        updated = self.store.get(session["id"])
        self.assertEqual([user["role"], assistant["role"]], ["user", "assistant"])
        self.assertEqual(session_summary(updated)["messageCount"], 2)

    def test_update_last_doc_rejects_bad_session_id(self) -> None:
        with self.assertRaises(ChatError):
            self.store.update_last_doc("../bad", "README.md")

    def test_delete_missing_session_reports_404(self) -> None:
        with self.assertRaises(ChatError) as raised:
            self.store.delete("missing123")
        self.assertEqual(raised.exception.status, 404)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the failing session tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_sessions
```

Expected: import failure for `scripts.notes.notes_chat_sessions`.

- [ ] **Step 3: Move session code**

Create `scripts/notes/notes_chat_sessions.py` by moving `SessionStore` and `session_summary` from `notes_chat_server.py` and importing helper functions from core:

```python
from __future__ import annotations

import json
import uuid
from pathlib import Path
from typing import Any

from scripts.notes.notes_chat_core import (
    ChatError,
    clean_session_title,
    now_iso,
    validate_session_id,
)

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SESSION_DIR = REPO_ROOT / ".tmp" / "notes-chat" / "sessions"
```

Move the existing `SessionStore` implementation unchanged, except for imports. Move `session_summary(...)` unchanged.

- [ ] **Step 4: Update server imports and construction**

In `notes_chat_server.py`, remove `SessionStore` and `session_summary`, then import:

```python
from scripts.notes.notes_chat_sessions import SESSION_DIR, SessionStore, session_summary
```

Keep:

```python
store = SessionStore(SESSION_DIR)
```

- [ ] **Step 5: Verify session extraction**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_core tests.notes.test_notes_chat_sessions
python3 -m py_compile scripts/notes/notes_chat_server.py scripts/notes/notes_chat_core.py scripts/notes/notes_chat_sessions.py
```

Expected: tests pass; compile succeeds.

- [ ] **Step 6: Commit Task 2**

```bash
git add scripts/notes/notes_chat_server.py scripts/notes/notes_chat_sessions.py tests/notes/test_notes_chat_sessions.py
git commit -m "Extract notes chat session storage"
```

## Task 3: Extract Existing Agent Protocols Without Changing Defaults

**Files:**
- Create: `tests/notes/test_notes_chat_agents.py`
- Create: `tests/notes/test_notes_chat_protocols.py`
- Create: `scripts/notes/notes_chat_agents.py`
- Create: `scripts/notes/notes_chat_protocols.py`
- Modify: `scripts/notes/notes_chat_server.py`

- [ ] **Step 1: Write binding and extraction tests**

Create `tests/notes/test_notes_chat_agents.py`:

```python
from __future__ import annotations

import os
import unittest
from unittest import mock

from scripts.notes.notes_chat_agents import make_adapter


class NotesChatAgentsTest(unittest.TestCase):
    def test_claude_binding_uses_default_command(self) -> None:
        adapter = make_adapter("claude", "", 12.0)
        health = adapter.health()
        self.assertEqual(health["name"], "claude")
        self.assertEqual(health["transport"], "claude-cli")
        self.assertEqual(health["command"], "claude")

    def test_acp_binding_requires_configured_command(self) -> None:
        adapter = make_adapter("acp", "fake-acp", 12.0)
        health = adapter.health()
        self.assertEqual(health["name"], "acp")
        self.assertEqual(health["transport"], "acp-stdio")
        self.assertEqual(health["command"], "fake-acp")

    def test_agent_command_overrides_environment(self) -> None:
        with mock.patch.dict(os.environ, {"NOTES_CHAT_CLAUDE_CMD": "env-claude"}):
            adapter = make_adapter("claude", "arg-claude", 12.0)
        self.assertEqual(adapter.health()["command"], "arg-claude")

    def test_unknown_agent_reports_error(self) -> None:
        with self.assertRaises(Exception) as raised:
            make_adapter("unknown", "", 12.0)
        self.assertIn("Unsupported agent backend", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
```

Create `tests/notes/test_notes_chat_protocols.py`:

```python
from __future__ import annotations

import unittest

from scripts.notes.notes_chat_protocols import (
    extract_acp_update_text,
    extract_claude_result_text,
    extract_claude_stream_text,
    extract_content_text,
)


class NotesChatProtocolsTest(unittest.TestCase):
    def test_extract_content_text_handles_nested_shapes(self) -> None:
        self.assertEqual(extract_content_text("hello"), "hello")
        self.assertEqual(extract_content_text([{"text": "a"}, {"delta": "b"}]), "ab")
        self.assertEqual(extract_content_text({"message": {"content": [{"text": "x"}]}}), "x")

    def test_extract_claude_stream_text_handles_content_delta(self) -> None:
        payload = {
            "type": "stream_event",
            "event": {
                "type": "content_block_delta",
                "delta": {"text": "chunk"},
            },
        }
        self.assertEqual(extract_claude_stream_text(payload), "chunk")

    def test_extract_claude_result_text_handles_result_string(self) -> None:
        self.assertEqual(extract_claude_result_text({"type": "result", "result": "done"}), "done")

    def test_extract_acp_update_text_handles_agent_message_chunk(self) -> None:
        params = {
            "update": {
                "sessionUpdate": "agent_message_chunk",
                "content": [{"text": "chunk"}],
            }
        }
        self.assertEqual(extract_acp_update_text(params), "chunk")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the failing protocol tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_agents tests.notes.test_notes_chat_protocols
```

Expected: import failures for `notes_chat_agents` and `notes_chat_protocols`.

- [ ] **Step 3: Move existing adapters and protocol helpers**

Create `scripts/notes/notes_chat_agents.py`:

```python
from __future__ import annotations

import os
from typing import Any, Iterable

from scripts.notes.notes_chat_core import ChatError, ChatRequest
from scripts.notes.notes_chat_protocols import AcpStdioProtocol, ClaudeCliProtocol


class AgentAdapter:
    name = "agent"

    def answer(self, request: ChatRequest) -> str:
        return "".join(self.stream(request)).strip()

    def stream(self, request: ChatRequest) -> Iterable[str]:
        raise NotImplementedError

    def health(self) -> dict[str, Any]:
        return {"name": self.name}


class ProtocolAgentAdapter(AgentAdapter):
    def __init__(self, name: str, protocol: Any) -> None:
        self.name = name
        self.protocol = protocol

    def health(self) -> dict[str, Any]:
        health = self.protocol.health()
        health["name"] = self.name
        return health

    def stream(self, request: ChatRequest) -> Iterable[str]:
        yield from self.protocol.stream(request)


def make_adapter(agent: str, command: str, timeout: float) -> AgentAdapter:
    if agent == "claude":
        return ProtocolAgentAdapter(
            "claude",
            ClaudeCliProtocol(command or os.environ.get("NOTES_CHAT_CLAUDE_CMD", "claude"), timeout),
        )
    if agent == "acp":
        return ProtocolAgentAdapter(
            "acp",
            AcpStdioProtocol(command or os.environ.get("NOTES_CHAT_ACP_CMD", ""), timeout),
        )
    raise ChatError(500, f"Unsupported agent backend: {agent}")
```

Create `scripts/notes/notes_chat_protocols.py` by moving:

```text
ClaudeCliAdapter -> ClaudeCliProtocol
AcpAdapter -> AcpStdioProtocol
AcpStdioClient
raise_acp_error
extract_acp_update_text
extract_content_text
extract_claude_stream_text
extract_claude_result_text
```

Rename protocol `health()` outputs so they do not set `name`; `ProtocolAgentAdapter.health()` will add it. Keep existing subprocess behavior unchanged.

- [ ] **Step 4: Update server to import `AgentAdapter` and `make_adapter`**

In `notes_chat_server.py`, remove existing `AgentAdapter`, `ClaudeCliAdapter`, `AcpAdapter`, `AcpStdioClient`, and extraction helpers.

Import:

```python
from scripts.notes.notes_chat_agents import AgentAdapter, make_adapter
```

Keep command-line choices as `["claude", "acp"]` for this task. Codex defaults change in Task 5.

- [ ] **Step 5: Verify existing adapters after extraction**

Run:

```bash
python3 -m unittest \
  tests.notes.test_notes_chat_core \
  tests.notes.test_notes_chat_sessions \
  tests.notes.test_notes_chat_agents \
  tests.notes.test_notes_chat_protocols
python3 -m py_compile scripts/notes/notes_chat_server.py scripts/notes/notes_chat_agents.py scripts/notes/notes_chat_protocols.py
```

Expected: tests pass; compile succeeds.

- [ ] **Step 6: Commit Task 3**

```bash
git add scripts/notes/notes_chat_server.py scripts/notes/notes_chat_agents.py scripts/notes/notes_chat_protocols.py tests/notes/test_notes_chat_agents.py tests/notes/test_notes_chat_protocols.py
git commit -m "Extract notes chat agent protocols"
```

## Task 4: Add MCP Stdio Protocol And Fake Server Tests

**Files:**
- Create: `tests/notes/fakes/fake_mcp_server.py`
- Modify: `tests/notes/test_notes_chat_protocols.py`
- Modify: `scripts/notes/notes_chat_protocols.py`

- [ ] **Step 1: Create a fake MCP server**

Create `tests/notes/fakes/fake_mcp_server.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys


def send(payload: dict) -> None:
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["ok", "missing-tool", "early-exit"], default="ok")
    args = parser.parse_args()
    if args.mode == "early-exit":
        print("fake mcp failed early", file=sys.stderr)
        return 7

    for line in sys.stdin:
        request = json.loads(line)
        method = request.get("method")
        request_id = request.get("id")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": request_id, "result": {"protocolVersion": "2024-11-05", "capabilities": {}}})
        elif method == "tools/list":
            tools = [] if args.mode == "missing-tool" else [{"name": "codex.prompt", "description": "Prompt Codex"}]
            send({"jsonrpc": "2.0", "id": request_id, "result": {"tools": tools}})
        elif method == "tools/call":
            params = request.get("params") or {}
            name = params.get("name")
            arguments = params.get("arguments") or {}
            text = "mcp answer: " + str(arguments.get("prompt", ""))[:20]
            send({"jsonrpc": "2.0", "id": request_id, "result": {"content": [{"type": "text", "text": text}], "tool": name}})
        else:
            send({"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": f"unknown method {method}"}})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Add MCP protocol tests**

Append to `tests/notes/test_notes_chat_protocols.py`:

```python
import pathlib
import sys

from scripts.notes.notes_chat_core import ChatError, ChatRequest, DocumentContext
from scripts.notes.notes_chat_protocols import McpStdioProtocol


def make_request() -> ChatRequest:
    return ChatRequest(
        document=DocumentContext("README.md", "Readme", "# Readme\nBody", False),
        message="Explain",
        selected_text="",
        history=[],
    )


class McpStdioProtocolTest(unittest.TestCase):
    def fake_server(self, mode: str) -> str:
        root = pathlib.Path(__file__).resolve().parents[2]
        return f"{sys.executable} {root / 'tests/notes/fakes/fake_mcp_server.py'} --mode {mode}"

    def test_mcp_protocol_streams_tool_result_text(self) -> None:
        protocol = McpStdioProtocol(self.fake_server("ok"), timeout=3.0)
        text = "".join(protocol.stream(make_request()))
        self.assertIn("mcp answer:", text)

    def test_mcp_protocol_reports_missing_compatible_tool(self) -> None:
        protocol = McpStdioProtocol(self.fake_server("missing-tool"), timeout=3.0)
        with self.assertRaises(ChatError) as raised:
            list(protocol.stream(make_request()))
        self.assertEqual(raised.exception.status, 502)
        self.assertIn("No compatible Codex MCP tool", raised.exception.message)
        self.assertIn("codex-exec", raised.exception.message)

    def test_mcp_protocol_reports_early_exit(self) -> None:
        protocol = McpStdioProtocol(self.fake_server("early-exit"), timeout=3.0)
        with self.assertRaises(ChatError) as raised:
            list(protocol.stream(make_request()))
        self.assertEqual(raised.exception.status, 502)
        self.assertIn("exited early", raised.exception.message)
```

- [ ] **Step 3: Run failing MCP tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_protocols.McpStdioProtocolTest
```

Expected: import failure for `McpStdioProtocol`.

- [ ] **Step 4: Implement `McpStdioProtocol`**

Add to `scripts/notes/notes_chat_protocols.py`:

```python
class McpStdioProtocol:
    def __init__(self, command: str, timeout: float, tool_candidates: list[str] | None = None) -> None:
        self.command = command
        self.timeout = timeout
        self.tool_candidates = tool_candidates or ["codex.prompt", "prompt", "chat", "codex"]
```

Implement methods equivalent to `AcpStdioClient` but with MCP method names:

```python
health()
stream(request)
_start()
_stop()
_request(method, params)
_next_message(deadline)
_read_stdout()
_read_stderr()
_select_tool(tools)
```

Use this tool call payload:

```python
{
    "name": selected_tool,
    "arguments": {"prompt": build_agent_prompt(request)},
}
```

Extract text from `result["content"]` with existing `extract_content_text(...)`.

If no candidate tool is found, raise:

```python
ChatError(
    502,
    "No compatible Codex MCP tool found. Available tools: "
    + ", ".join(available)
    + ". Try --chat-agent codex-exec.",
)
```

- [ ] **Step 5: Verify MCP protocol**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_protocols
python3 -m py_compile scripts/notes/notes_chat_protocols.py tests/notes/fakes/fake_mcp_server.py
```

Expected: tests pass; compile succeeds.

- [ ] **Step 6: Commit Task 4**

```bash
git add scripts/notes/notes_chat_protocols.py tests/notes/test_notes_chat_protocols.py tests/notes/fakes/fake_mcp_server.py
git commit -m "Add notes chat MCP stdio protocol"
```

## Task 5: Add Codex Agent Binding And Make It Default

**Files:**
- Modify: `tests/notes/test_notes_chat_agents.py`
- Create: `tests/notes/test_notes_serve_scripts.py`
- Modify: `scripts/notes/notes_chat_agents.py`
- Modify: `scripts/notes/notes_chat_server.py`
- Modify: `scripts/notes/serve_site.sh`
- Modify: `scripts/notes/serve_site.ps1`

- [ ] **Step 1: Add Codex binding tests**

Append to `tests/notes/test_notes_chat_agents.py`:

```python
    def test_codex_binding_defaults_to_mcp_stdio(self) -> None:
        adapter = make_adapter("codex", "", 12.0)
        health = adapter.health()
        self.assertEqual(health["name"], "codex")
        self.assertEqual(health["transport"], "mcp-stdio")
        self.assertEqual(health["command"], "codex mcp-server")

    def test_codex_command_environment_override(self) -> None:
        with mock.patch.dict(os.environ, {"NOTES_CHAT_CODEX_CMD": "custom-codex mcp-server"}):
            adapter = make_adapter("codex", "", 12.0)
        self.assertEqual(adapter.health()["command"], "custom-codex mcp-server")
```

Create `tests/notes/test_notes_serve_scripts.py`:

```python
from __future__ import annotations

import pathlib
import re
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


class NotesServeScriptsTest(unittest.TestCase):
    def test_shell_script_defaults_to_codex_and_accepts_all_agents(self) -> None:
        text = (REPO_ROOT / "scripts/notes/serve_site.sh").read_text(encoding="utf-8")
        self.assertIn('CHAT_AGENT="${NOTES_CHAT_AGENT:-codex}"', text)
        self.assertIn('"codex"', text)
        self.assertIn('"codex-exec"', text)
        self.assertIn('"claude"', text)
        self.assertIn('"acp"', text)
        self.assertNotIn("must be claude or acp", text)

    def test_powershell_script_defaults_to_codex_and_accepts_all_agents(self) -> None:
        text = (REPO_ROOT / "scripts/notes/serve_site.ps1").read_text(encoding="utf-8")
        self.assertIn('else { "codex" }', text)
        validate_set = re.search(r'\[ValidateSet\(([^\)]*)\)\]', text)
        self.assertIsNotNone(validate_set)
        values = validate_set.group(1)
        self.assertIn('"codex"', values)
        self.assertIn('"codex-exec"', values)
        self.assertIn('"claude"', values)
        self.assertIn('"acp"', values)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run failing default tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_agents tests.notes.test_notes_serve_scripts
```

Expected: Codex binding and default assertions fail.

- [ ] **Step 3: Add Codex binding**

In `scripts/notes/notes_chat_agents.py`, import `McpStdioProtocol` and add:

```python
if agent == "codex":
    return ProtocolAgentAdapter(
        "codex",
        McpStdioProtocol(command or os.environ.get("NOTES_CHAT_CODEX_CMD", "codex mcp-server"), timeout),
    )
```

- [ ] **Step 4: Update server and serve script choices**

In `scripts/notes/notes_chat_server.py`, change argparse:

```python
parser.add_argument(
    "--agent",
    choices=["codex", "codex-exec", "claude", "acp"],
    default=os.environ.get("NOTES_CHAT_AGENT", "codex"),
)
```

In `scripts/notes/serve_site.sh`:

```bash
CHAT_AGENT="${NOTES_CHAT_AGENT:-codex}"
```

Change validation to:

```bash
if [[ "${CHAT_AGENT}" != "codex" && "${CHAT_AGENT}" != "codex-exec" && "${CHAT_AGENT}" != "claude" && "${CHAT_AGENT}" != "acp" ]]; then
    echo "Error: --chat-agent must be codex, codex-exec, claude, or acp" >&2
    exit 1
fi
```

In `scripts/notes/serve_site.ps1`, change the parameter to:

```powershell
[ValidateSet("codex", "codex-exec", "claude", "acp")]
[string]$ChatAgent = $(if ($env:NOTES_CHAT_AGENT) { $env:NOTES_CHAT_AGENT } else { "codex" })
```

Update top comments in both scripts from “default Claude” to “default Codex”.

- [ ] **Step 5: Verify Codex default**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_agents tests.notes.test_notes_serve_scripts
bash -n scripts/notes/serve_site.sh
python3 -m py_compile scripts/notes/notes_chat_server.py scripts/notes/notes_chat_agents.py
```

Expected: tests pass; syntax checks pass.

- [ ] **Step 6: Commit Task 5**

```bash
git add scripts/notes/notes_chat_agents.py scripts/notes/notes_chat_server.py scripts/notes/serve_site.sh scripts/notes/serve_site.ps1 tests/notes/test_notes_chat_agents.py tests/notes/test_notes_serve_scripts.py
git commit -m "Default notes chat to Codex MCP"
```

## Task 6: Add Codex Exec Fallback

**Files:**
- Create: `tests/notes/fakes/fake_jsonl_agent.py`
- Modify: `tests/notes/test_notes_chat_agents.py`
- Modify: `tests/notes/test_notes_chat_protocols.py`
- Modify: `scripts/notes/notes_chat_agents.py`
- Modify: `scripts/notes/notes_chat_protocols.py`

- [ ] **Step 1: Create fake JSONL agent**

Create `tests/notes/fakes/fake_jsonl_agent.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["ok", "bad-json", "fail"], default="ok")
    args = parser.parse_args()
    prompt = sys.stdin.read()
    if args.mode == "fail":
        print("fake jsonl failure", file=sys.stderr)
        return 9
    if args.mode == "bad-json":
        print("{not json")
        return 0
    print(json.dumps({"type": "assistant_delta", "delta": "exec "}))
    print(json.dumps({"type": "assistant_delta", "delta": "answer"}))
    print(json.dumps({"type": "result", "result": "exec answer"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Add fallback tests**

Append to `tests/notes/test_notes_chat_agents.py`:

```python
    def test_codex_exec_binding_uses_cli_jsonl(self) -> None:
        adapter = make_adapter("codex-exec", "", 12.0)
        health = adapter.health()
        self.assertEqual(health["name"], "codex-exec")
        self.assertEqual(health["transport"], "cli-jsonl")
        self.assertIn("codex exec --json", health["command"])
```

Append to `tests/notes/test_notes_chat_protocols.py`:

```python
from scripts.notes.notes_chat_protocols import CliJsonProtocol


class CliJsonProtocolTest(unittest.TestCase):
    def fake_agent(self, mode: str) -> str:
        root = pathlib.Path(__file__).resolve().parents[2]
        return f"{sys.executable} {root / 'tests/notes/fakes/fake_jsonl_agent.py'} --mode {mode}"

    def test_cli_json_protocol_streams_jsonl_delta_text(self) -> None:
        protocol = CliJsonProtocol(self.fake_agent("ok"), timeout=3.0)
        self.assertEqual("".join(protocol.stream(make_request())), "exec answer")

    def test_cli_json_protocol_reports_bad_json(self) -> None:
        protocol = CliJsonProtocol(self.fake_agent("bad-json"), timeout=3.0)
        with self.assertRaises(ChatError) as raised:
            list(protocol.stream(make_request()))
        self.assertEqual(raised.exception.status, 502)
        self.assertIn("Invalid JSONL", raised.exception.message)

    def test_cli_json_protocol_reports_nonzero_exit(self) -> None:
        protocol = CliJsonProtocol(self.fake_agent("fail"), timeout=3.0)
        with self.assertRaises(ChatError) as raised:
            list(protocol.stream(make_request()))
        self.assertEqual(raised.exception.status, 502)
        self.assertIn("exited with code 9", raised.exception.message)
```

- [ ] **Step 3: Run failing fallback tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_agents tests.notes.test_notes_chat_protocols.CliJsonProtocolTest
```

Expected: import failure for `CliJsonProtocol` and missing `codex-exec` binding.

- [ ] **Step 4: Implement `CliJsonProtocol`**

Add to `scripts/notes/notes_chat_protocols.py`:

```python
class CliJsonProtocol:
    def __init__(self, command: str, timeout: float) -> None:
        self.command = command
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        executable = shlex.split(self.command)[0] if self.command else ""
        return {
            "transport": "cli-jsonl",
            "command": self.command,
            "available": bool(executable and shutil.which(executable)),
            "streaming": True,
        }
```

Implement `stream(...)` by starting the command, writing `build_agent_prompt(request)` to stdin, reading JSONL stdout, and extracting text with:

```python
def extract_cli_json_text(payload: dict[str, Any]) -> str:
    if isinstance(payload.get("delta"), str):
        return payload["delta"]
    if isinstance(payload.get("message"), str):
        return payload["message"]
    if isinstance(payload.get("text"), str):
        return payload["text"]
    if payload.get("type") == "result":
        return extract_content_text(payload.get("result"))
    return extract_content_text(payload.get("message"))
```

Accumulate deltas. If a final result is present, prefer it to avoid duplicate text from JSONL delta plus result events.

- [ ] **Step 5: Add `codex-exec` binding**

In `scripts/notes/notes_chat_agents.py`, import `CliJsonProtocol` and add:

```python
if agent == "codex-exec":
    return ProtocolAgentAdapter(
        "codex-exec",
        CliJsonProtocol(
            command
            or os.environ.get(
                "NOTES_CHAT_CODEX_EXEC_CMD",
                "codex exec --json --ephemeral --sandbox read-only --ask-for-approval never",
            ),
            timeout,
        ),
    )
```

- [ ] **Step 6: Verify fallback**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_agents tests.notes.test_notes_chat_protocols
python3 -m py_compile scripts/notes/notes_chat_protocols.py tests/notes/fakes/fake_jsonl_agent.py
```

Expected: tests pass; compile succeeds.

- [ ] **Step 7: Commit Task 6**

```bash
git add scripts/notes/notes_chat_agents.py scripts/notes/notes_chat_protocols.py tests/notes/test_notes_chat_agents.py tests/notes/test_notes_chat_protocols.py tests/notes/fakes/fake_jsonl_agent.py
git commit -m "Add Codex exec fallback for notes chat"
```

## Task 7: Server Compatibility, Documentation, And Final Verification

**Files:**
- Create: `tests/notes/test_notes_chat_server.py`
- Modify: `notes/tools/notes-tooling.md`

- [ ] **Step 1: Write server compatibility tests**

Create `tests/notes/test_notes_chat_server.py`:

```python
from __future__ import annotations

import json
import tempfile
import threading
import unittest
import urllib.request
from pathlib import Path

from scripts.notes.notes_chat_agents import AgentAdapter
from scripts.notes.notes_chat_core import ChatRequest
from scripts.notes.notes_chat_server import NotesChatServer
from scripts.notes.notes_chat_sessions import SessionStore


class FakeAdapter(AgentAdapter):
    name = "fake"

    def health(self) -> dict[str, object]:
        return {"name": self.name, "transport": "fake", "available": True, "streaming": True}

    def stream(self, request: ChatRequest):
        yield "hello "
        yield request.document.title


class NotesChatServerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.notes_dir = self.root / "notes"
        self.notes_dir.mkdir()
        (self.notes_dir / "README.md").write_text("# Home\nBody", encoding="utf-8")

        self.notes_patch = unittest.mock.patch("scripts.notes.notes_chat_core.NOTES_DIR", self.notes_dir)
        self.notes_patch.start()
        self.server = NotesChatServer(("127.0.0.1", 0), FakeAdapter(), SessionStore(self.root / "sessions"), 60000)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        host, port = self.server.server_address
        self.base_url = f"http://{host}:{port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.notes_patch.stop()
        self.tmp.cleanup()

    def request_json(self, path: str, payload: dict | None = None) -> dict:
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + path,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST" if payload is not None else "GET",
        )
        with urllib.request.urlopen(request, timeout=3.0) as response:
            return json.loads(response.read().decode("utf-8"))

    def test_health_reports_agent(self) -> None:
        payload = self.request_json("/health")
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["agent"]["name"], "fake")
        self.assertTrue(payload["readOnly"])

    def test_chat_json_shape_is_unchanged(self) -> None:
        payload = self.request_json("/chat", {"message": "hello", "docPath": "README.md"})
        self.assertEqual(payload["answer"], "hello Home")
        self.assertEqual(payload["agent"], "fake")
        self.assertEqual(payload["docPath"], "README.md")
        self.assertIn("session", payload)
        self.assertEqual(len(payload["messages"]), 2)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run server compatibility tests**

Run:

```bash
python3 -m unittest tests.notes.test_notes_chat_server
```

Expected: pass after earlier refactors; if import paths fail, update imports in the test or server to match the modules created in Tasks 1-6.

- [ ] **Step 3: Update notes tooling documentation**

In `notes/tools/notes-tooling.md`, add a section after `scripts/notes/serve_site.sh`:

```markdown
### Notes Chat agent

`notes_chat` 默认使用 Codex：

```bash
scripts/notes/serve_site.sh --chat
```

等价于：

```bash
NOTES_CHAT_AGENT=codex scripts/notes/serve_site.sh --chat
```

当前可选 agent：

| agent | 协议 | 典型用途 |
|---|---|---|
| `codex` | `codex mcp-server` / MCP stdio | 默认路径 |
| `codex-exec` | `codex exec --json` | Codex MCP 工具不兼容时的 fallback |
| `claude` | Claude CLI stream JSON | 保留旧路径 |
| `acp` | ACP stdio | 兼容 ACP agent |

网页 Chat 仍然是只读入口。它会把当前文档、选中文本和会话历史交给 agent，但不会暴露文件写入或终端执行能力。
```
```

- [ ] **Step 4: Run full notes chat test suite**

Run:

```bash
python3 -m unittest discover -s tests/notes -v
python3 -m py_compile scripts/notes/notes_chat_server.py scripts/notes/notes_chat_core.py scripts/notes/notes_chat_sessions.py scripts/notes/notes_chat_agents.py scripts/notes/notes_chat_protocols.py scripts/notes/watch_site_inputs.py
bash -n scripts/notes/serve_site.sh
scripts/notes/serve_site.sh --build
```

Expected: tests pass; compile succeeds; shell syntax succeeds; notes site builds. Existing MkDocs warnings unrelated to this change may remain.

- [ ] **Step 5: Run focused existing regression**

Run:

```bash
python3 -m unittest tests.lxe_editor.test_mcp_config
```

Expected: existing MCP config tests still pass.

- [ ] **Step 6: Commit Task 7**

```bash
git add tests/notes/test_notes_chat_server.py notes/tools/notes-tooling.md
git commit -m "Document Codex notes chat backend"
```

## Final Verification

Run:

```bash
python3 -m unittest discover -s tests/notes -v
python3 -m unittest tests.lxe_editor.test_mcp_config
python3 -m py_compile \
  scripts/notes/notes_chat_server.py \
  scripts/notes/notes_chat_core.py \
  scripts/notes/notes_chat_sessions.py \
  scripts/notes/notes_chat_agents.py \
  scripts/notes/notes_chat_protocols.py \
  scripts/notes/watch_site_inputs.py
bash -n scripts/notes/serve_site.sh
scripts/notes/serve_site.sh --build
git status --short
```

Expected:

- Notes chat tests pass.
- Existing MCP config tests pass.
- Python compile checks pass.
- Shell syntax check passes.
- Notes site build exits 0.
- `git status --short` shows no uncommitted implementation files after all task commits.

## Self-Review Notes

- Spec coverage: module split is covered by Tasks 1-3; Codex MCP default by Tasks 4-5; `codex-exec` fallback by Task 6; docs and compatibility by Task 7.
- Protocol isolation: all process protocols live in `notes_chat_protocols.py`; agent name binding lives in `notes_chat_agents.py`; HTTP routes stay in `notes_chat_server.py`.
- Testing: fake MCP and fake JSONL processes avoid requiring real Codex login or network access.
- API compatibility: Task 7 verifies `/health` and `/chat`; SSE event names remain in `notes_chat_server.py` and should not be renamed during implementation.
