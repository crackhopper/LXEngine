# Notes Chat Codex Agent Design

## Goal

Make the notes web chat use Codex by default while keeping Claude and ACP available. The current implementation is centered on `ClaudeCliAdapter` inside `scripts/notes/notes_chat_server.py`; the new design should split the chat service into smaller modules and introduce a protocol layer so each coding agent binds to a concrete transport implementation.

The default path is:

```text
notes chat -> codex agent binding -> MCP stdio protocol -> codex mcp-server
```

## Scope

### In scope

- Split `scripts/notes/notes_chat_server.py` into focused modules.
- Add a protocol abstraction underneath the existing `AgentAdapter` shape.
- Add a Codex binding that defaults to MCP stdio via `codex mcp-server`.
- Add a `codex-exec` fallback binding using `codex exec --json` for environments where the MCP tool surface is not compatible.
- Keep Claude and ACP working through the same high-level `ChatRequest -> stream text` interface.
- Change default notes chat agent from `claude` to `codex`.
- Update Linux and PowerShell serve scripts to accept `codex`, `codex-exec`, `claude`, and `acp`.
- Add tests for protocol behavior, agent binding, script argument validation, and HTTP/SSE compatibility.

### Out of scope

- Giving the web chat write access to the repository.
- Exposing Codex's full coding workflow through the browser chat.
- Changing the frontend chat API shape.
- Replacing the existing `lxe_manager` MCP workflow.
- Requiring a real Codex login in tests.

## Current State

The existing chat service has three responsibilities mixed in one file:

- HTTP routes and SSE/JSON response handling.
- Notes document resolution, prompt building, and session persistence.
- Agent adapters for Claude CLI and ACP stdio.

Current defaults are Claude-oriented:

- `scripts/notes/serve_site.sh` defaults `CHAT_AGENT="${NOTES_CHAT_AGENT:-claude}"`.
- `scripts/notes/serve_site.ps1` defaults `$ChatAgent` to `claude`.
- `notes_chat_server.py --agent` accepts `claude|acp` and defaults to `claude`.

The local Codex CLI supports two useful integration paths:

- `codex mcp-server`: starts Codex as a stdio MCP server.
- `codex exec --json`: runs a non-interactive session and emits JSONL events.

## Architecture

The service should have three layers.

### Notes Chat Core

This layer is agent-agnostic. It owns only notes-specific behavior:

- `DocumentContext`
- `ChatRequest`
- document path validation and Markdown loading
- prompt construction
- history formatting
- session title cleanup

Proposed file:

```text
scripts/notes/notes_chat_core.py
```

### Session Store

Session storage should be isolated from HTTP handling and protocols.

Proposed file:

```text
scripts/notes/notes_chat_sessions.py
```

It owns:

- `SessionStore`
- session id validation
- session summary generation
- JSON file read/write under `.tmp/notes-chat/sessions/`

### Agent And Protocol Layer

The public adapter interface remains narrow:

```python
class AgentAdapter:
    name: str
    def health(self) -> dict[str, Any]: ...
    def stream(self, request: ChatRequest) -> Iterable[str]: ...
```

Agent bindings map names to protocol implementations:

| Agent | Protocol | Default command |
|---|---|---|
| `codex` | MCP stdio | `codex mcp-server` |
| `codex-exec` | CLI JSONL | `codex exec --json --ephemeral --sandbox read-only --ask-for-approval never` |
| `claude` | Claude CLI streaming JSON | `claude` |
| `acp` | ACP stdio | `NOTES_CHAT_ACP_CMD` / explicit command |

Proposed files:

```text
scripts/notes/notes_chat_agents.py
scripts/notes/notes_chat_protocols.py
```

`notes_chat_agents.py` should own names, defaults, environment variable lookup, and `make_adapter(...)`.

`notes_chat_protocols.py` should own process protocols:

- `McpStdioProtocol`
- `CliJsonProtocol`
- `ClaudeCliProtocol`
- `AcpStdioProtocol`

The HTTP server remains in:

```text
scripts/notes/notes_chat_server.py
```

After the split it should primarily contain:

- `NotesChatHandler`
- `NotesChatServer`
- request body parsing
- response/SSE helpers
- `main()`

## Codex MCP Protocol

`McpStdioProtocol` starts the configured command and speaks JSON-RPC over stdio.

Minimum flow:

1. Start process in repo root.
2. Send `initialize`.
3. Send `tools/list`.
4. Select a compatible Codex prompt tool.
5. Send `tools/call` with the read-only prompt.
6. Extract text content from the result.
7. Terminate the process after the request.

The first implementation should include a small tool-selection layer instead of hardcoding only one tool name. It should accept a short ordered candidate list and return a clear error if none are present. The error should include the available tool names and suggest `--chat-agent codex-exec`.

The MCP protocol should reject or ignore server messages that require client-side file or terminal actions. The prompt itself already says the chat is read-only; the protocol layer should preserve that boundary by not implementing any client methods that mutate the workspace.

## Codex Exec Fallback

`codex-exec` is a fallback agent binding, not the default.

Default command:

```text
codex exec --json --ephemeral --sandbox read-only --ask-for-approval never
```

The prompt is written to stdin. The protocol reads JSONL events and extracts assistant text/final response. If the JSON event shape changes, the adapter should fail with a diagnostic that includes the unknown event type and a short stderr tail.

This fallback is useful for validating that Codex can answer read-only questions even when the MCP server's tool surface is not compatible with the notes chat client.

## Configuration

Defaults:

| Setting | New value |
|---|---|
| `NOTES_CHAT_AGENT` | `codex` |
| `serve_site.sh --chat-agent` | `codex|codex-exec|claude|acp` |
| `serve_site.ps1 -ChatAgent` | `codex|codex-exec|claude|acp` |
| `notes_chat_server.py --agent` | `codex|codex-exec|claude|acp` |

Environment variables:

| Variable | Purpose |
|---|---|
| `NOTES_CHAT_CODEX_CMD` | Codex MCP command, default `codex mcp-server` |
| `NOTES_CHAT_CODEX_EXEC_CMD` | Codex exec command, default listed above |
| `NOTES_CHAT_CLAUDE_CMD` | Existing Claude command override |
| `NOTES_CHAT_ACP_CMD` | Existing ACP command override |
| `NOTES_CHAT_AGENT_COMMAND` | Explicit serve-script override passed to the chosen agent |

`--chat-agent-command` keeps its current meaning: override the command for the selected agent.

## HTTP And Frontend Compatibility

The existing frontend should not need an API change.

Preserve:

- `GET /health`
- `GET /sessions`
- `POST /sessions`
- `DELETE /sessions/<id>`
- `GET /doc`
- `POST /chat`
- `POST /chat/stream`
- SSE events: `session`, `delta`, `done`, `error`

`/health` should continue returning:

```json
{
  "ok": true,
  "readOnly": true,
  "agent": {
    "name": "codex",
    "transport": "mcp-stdio",
    "command": "codex mcp-server",
    "available": true,
    "streaming": true
  }
}
```

## Error Handling

Errors should be actionable:

- Missing executable: return `503` and name the missing command.
- MCP initialization failure: return `502` with a short stderr tail.
- No compatible Codex MCP tool: return `502`, include available tool names, and suggest `codex-exec`.
- Timeout: return `504`, terminate the child process, and preserve the session error message.
- Client disconnect: preserve existing `499` behavior.

Child processes must be terminated on normal completion, timeout, server error, or client disconnect.

## Testing

Tests should not require a real Codex login.

### Unit Tests

Add tests under `tests/notes/` or another existing Python test location for:

- `McpStdioProtocol` with a fake MCP server that supports `initialize`, `tools/list`, and `tools/call`.
- MCP missing-tool diagnostics.
- MCP process early exit diagnostics.
- `CliJsonProtocol` with a fake JSONL process.
- Codex agent binding defaults to MCP stdio.
- `codex-exec` binding uses CLI JSONL.
- Claude and ACP bindings still construct correctly.
- Prompt building remains read-only and includes document/history context.

### Script Tests

Cover:

- `scripts/notes/serve_site.sh --chat-agent codex`
- `scripts/notes/serve_site.sh --chat-agent codex-exec`
- invalid agent still fails.
- PowerShell `ValidateSet` includes `codex` and `codex-exec`.
- Default `NOTES_CHAT_AGENT` is `codex`.

### Server Tests

Use fake adapters to ensure:

- `POST /chat` response shape is unchanged.
- `POST /chat/stream` emits the same event names.
- `GET /health` reports agent health.
- session persistence is unchanged after module split.

## Implementation Order

1. Add tests around current behavior where missing.
2. Extract core prompt/document/session code without behavior changes.
3. Extract existing Claude and ACP protocol code behind the new protocol layer.
4. Add `McpStdioProtocol` and fake MCP tests.
5. Add `codex` binding and change defaults.
6. Add `CliJsonProtocol` and `codex-exec` fallback.
7. Update serve scripts and docs.
8. Run notes build and Python tests.

## Acceptance Criteria

- Starting notes chat without overrides uses `codex`.
- `GET /health` reports `agent.name == "codex"` and `transport == "mcp-stdio"` when Codex is available.
- `--chat-agent claude` still works.
- `--chat-agent acp` still works.
- `--chat-agent codex-exec` works with fake JSONL tests and is available as a fallback.
- The browser chat API and SSE event names are unchanged.
- Tests cover protocol selection, fake MCP success, fake MCP missing tool, fake exec JSONL, and server route compatibility.
- No tests require network access or real Codex authentication.

## Open Risks

- Codex MCP server's exact tool names or result shapes may change. The implementation should use tool discovery and diagnostics rather than hardcoded assumptions.
- If `codex mcp-server` expects a richer MCP client than notes chat needs, `codex-exec` remains the fallback.
- Streaming over MCP may not map one-to-one to current SSE deltas. The first version may emit the final result as one `delta`; streaming can be improved later without changing the HTTP API.
