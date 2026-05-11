# lxe_editor HTTP MCP Design

## Context

`lxe_editor` currently exposes MCP through a custom raw TCP socket server and
relies on a local Python bridge process so Codex can consume it as a stdio MCP
server. That shape is repository-specific and does not match the documented
Codex MCP URL flow. The editor already has an HTTP API surface and token model,
so MCP should move onto the same HTTP server as a first-class URL endpoint.

## Goals

- Remove the raw TCP MCP transport entirely.
- Remove the local MCP bridge process entirely.
- Expose MCP through an HTTP URL endpoint on the existing editor HTTP server.
- Reuse the existing bearer-token authentication model for both `/api/...` and
  `/mcp`.
- Rename `automation server` terminology to `api server`.
- Keep local and remote Codex usage on the same direct URL-based MCP flow.

## Non-Goals

- Do not redesign the underlying command surface or editor control semantics.
- Do not introduce a second HTTP port just for MCP.
- Do not keep TCP MCP compatibility.
- Do not keep bridge-based Codex integration as a fallback path.

## Architecture

### API Server

`lxe_editor` SHALL expose one HTTP server as the external control surface.

That server SHALL host:

- `GET /health`
- existing `/api/...` endpoints
- new MCP endpoint at `POST /mcp`

The editor SHALL describe this service as the `API server`, not the
`automation server`.

### MCP Handler Core

MCP request handling SHALL move into a transport-agnostic handler that accepts:

- the parsed MCP request payload
- access to the current `LxeEditorApiService`

and produces:

- the MCP response payload

This handler SHALL implement current MCP behavior:

- `initialize`
- `ping`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/read`
- `prompts/list`

The HTTP layer SHALL only:

- authenticate the request
- parse the request body
- invoke the MCP handler
- return the MCP response

### Authentication

MCP over HTTP SHALL authenticate at the HTTP layer using the same bearer token
as the editor API:

- header: `Authorization: Bearer <token>`

The MCP payload itself SHALL NOT carry authentication state.

The previous `initialize.params.token` injection flow SHALL be removed.

### Runtime State

`runtime_state.yaml` SHALL stop advertising raw TCP MCP fields:

- remove `mcpHost`
- remove `mcpPort`

It SHALL instead advertise the HTTP MCP endpoint:

- `apiHost`
- `apiPort`
- `mcpUrl`
- `tokenFile`

Example:

```yaml
version: 1
pid: 12345
apiHost: 0.0.0.0
apiPort: 3768
mcpUrl: http://127.0.0.1:3768/mcp
tokenFile: /path/to/data/lxe_editor/automation_token.txt
startedAt: 2026-05-11-210000
```

## Naming

The following naming cleanup SHALL happen as part of the migration:

- `EditorAutomationServer` -> `LxeEditorApiServer`
- `EditorAutomationService` -> `LxeEditorApiService`
- `EditorAutomationProtocol` -> `LxeEditorApiProtocol`

Equivalent test names, logs, and README language SHALL move from
`automation` to `api` when they refer to the editor HTTP surface.

This migration does not require renaming every historically named data struct in
one pass, but the public HTTP/MCP surface SHALL use `api` terminology.

## Codex Integration

Codex SHALL consume `lxe_editor` MCP directly through a URL in
`.codex/config.toml`, not through a stdio bridge process.

The target MCP URL SHALL be configurable through environment-driven local or
remote workflows, matching the current operator preference of using shell
environment setup instead of committing secrets to config files.

The repository SHALL no longer require:

- `.codex/scripts/lxe_editor_mcp_bridge.py`
- local/remote bridge helper scripts

## Migration

The implementation SHALL proceed in this order:

1. Extract MCP request handling from the TCP server into a reusable handler.
2. Add `POST /mcp` to the HTTP API server using bearer-token auth.
3. Switch runtime state output to HTTP MCP URL fields.
4. Update `.codex/config.toml` to use direct MCP URL configuration.
5. Remove the TCP MCP server and all bridge scripts.
6. Rename HTTP server terminology from `automation` to `api`.

## Testing

### Keep

Keep lower-level C++ tests that verify:

- command and state logic
- API service behavior
- protocol serialization rules

### Replace

Replace TCP/bridge-oriented tests with HTTP MCP tests that exercise:

- `initialize`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/read`
- unauthorized access returning HTTP 401

### Black-Box

Python black-box tests SHALL verify direct HTTP MCP usage through `/mcp`
without any bridge process.

## Acceptance Criteria

- `lxe_editor` exposes MCP at `http://<host>:<port>/mcp`.
- Codex can connect to that MCP endpoint through URL configuration alone.
- Local and remote usage no longer require a Python bridge.
- MCP requests authenticate with the same bearer token as `/api/...`.
- Raw TCP MCP code is removed.
- Existing MCP tools and resources still work through HTTP MCP.
