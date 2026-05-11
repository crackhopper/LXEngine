---
name: lxe-editor-debug
description: Inspect and debug a running lxe_editor instance through the repo-local lxe_editor MCP server and its lxe_editor_* tools/resources.
---

Use this skill when the task is to inspect, diagnose, or drive a running `lxe_editor`
instance through MCP instead of re-implementing editor probing logic in shell code.

## Scope

This skill assumes the repo-local Codex config registers an MCP server named
`lxe_editor`. The server connection is discovered from
`data/lxe_editor/runtime_state.yaml` through `.codex/scripts/lxe_editor_mcp_bridge.py`.

This skill does not replace source-side MCP implementation. If the editor has
not written `runtime_state.yaml`, or if the in-process localhost MCP server is
not listening yet, report that as the blocker.

## Preferred Surfaces

Prefer stable MCP resources for reads:

- `lxe-editor://summary`
- `lxe-editor://selection`
- `lxe-editor://cameras`
- `lxe-editor://toolbar`
- `lxe-editor://scene`

Use tools for actions and active polling:

- `lxe_editor_ensure_running`
- `lxe_editor_get_summary`
- `lxe_editor_get_selection`
- `lxe_editor_get_cameras`
- `lxe_editor_pick`
- `lxe_editor_command`
- `lxe_editor_wait_for`

## Workflow

1. Check that the `lxe_editor` MCP server is available in the current session.
2. If MCP resources are available, read `lxe-editor://summary` first to anchor
   the current scene, mode, preview state, dirty bit, and active camera.
3. Read narrower resources such as `lxe-editor://selection` or
   `lxe-editor://cameras` before issuing commands.
4. Use `lxe_editor_command` only for actions that are naturally expressed as
   command-bus text.
5. Use `lxe_editor_pick` for coordinate-driven probing instead of synthesizing
   lower-level HTTP requests.
6. After an action, verify the result with a resource read or
   `lxe_editor_wait_for`; do not assume the editor state changed as requested.

## Guardrails

- Prefer MCP over direct file scraping once the server is connected.
- Do not maintain a second debug protocol in the skill.
- If tools or resources are missing, distinguish between:
  - repo-local registration missing
  - `data/lxe_editor/runtime_state.yaml` missing
  - localhost MCP socket not listening yet
  - source-side server connected but missing a specific `lxe_editor_*` tool or
    `lxe-editor://` resource
- Keep resource names and tool names aligned with the approved design:
  `lxe_editor_*` for tools, `lxe-editor://...` for resources.

## Typical Debug Sequence

1. Ensure the editor is running.
2. Read `lxe-editor://summary`.
3. Read `lxe-editor://selection` or `lxe-editor://scene` if the issue is about
   picking, selection, or document state.
4. Issue one command or pick action.
5. Re-read the affected resource or wait for the expected state.

## Failure Reporting

When MCP access fails, report the smallest true blocker:

- `runtime_state.yaml` absent: the local editor runtime has not published MCP discovery data.
- MCP socket connect failed: the source-side localhost MCP server is not live yet.
- Tool/resource absent: the server is reachable, but the requested MCP surface is not implemented yet.
