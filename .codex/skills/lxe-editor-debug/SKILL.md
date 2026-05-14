---
name: lxe-editor-debug
description: Use when inspecting a running lxe_editor through lxe_manager MCP for state reads, lightweight command-bus actions, pick probes, or wait-for polling.
---

# lxe-editor-debug

Use this skill for ordinary editor state inspection and small interactive probes.
Do not use it for recording/replay, build-version comparison, manager operations,
or broad command discovery; those have separate skills.

## Scope

This skill assumes the repo-local Codex config registers an MCP server named
`lxe_manager`. If MCP is unavailable, report the smallest concrete blocker:
missing config, bad URL/token, manager not listening, or editor not discovered.

## Preferred Surfaces

Prefer stable MCP resources for reads:

- `lxe-editor://summary`
- `lxe-editor://selection`
- `lxe-editor://cameras`
- `lxe-editor://toolbar`
- `lxe-editor://scene`

Use only these tools for debug actions and active polling:

- `lxe_editor_ensure_running`
- `lxe_editor_get_summary`
- `lxe_editor_get_selection`
- `lxe_editor_get_cameras`
- `lxe_editor_pick`
- `lxe_editor_command`
- `lxe_editor_wait_for`

For command syntax beyond obvious one-line actions, load
`lxe-editor-command-reference` instead of guessing.

MCP should stay a stable transport layer. When a new editor operation can be
expressed as a command-console action, prefer adding or using a command-bus
command and documenting it in skills instead of expanding manager MCP protocol
surface. That keeps a running manager usable without restart.

## Workflow

1. Check that the `lxe_manager` MCP server is available in the current session.
2. If MCP resources are available, read `lxe-editor://summary` first to anchor
   the current scene, mode, preview state, dirty bit, and active camera.
3. For scene-specific debugging, confirm the summary has a loaded scene or
   current document. If not, load the intended test scene before probing. Use
   `lxe-editor-command-reference` to verify `scene load` syntax when needed.
4. Read narrower resources such as `lxe-editor://selection` or
   `lxe-editor://cameras` before issuing commands.
5. Use `lxe_editor_command` only for small, reversible or already-approved
   command-bus actions.
6. Use `lxe_editor_pick` for coordinate-driven probing instead of synthesizing
   lower-level HTTP requests.
7. After an action, verify the result with a resource read or
   `lxe_editor_wait_for`; do not assume the editor state changed as requested.

If `lxe_editor_ensure_running` or a state read returns `editor_unavailable`,
switch to `lxe-manager-ops` and call `ops.editor_status`. That is the canonical
way to tell whether the editor is simply not started.

## Evidence-First Debugging

When debugging crashes, unexpected editor exits, failed scene loads, or remote
MCP/editor mismatches, do not guess the root cause from symptoms. Establish
evidence before changing editor behavior:

1. Reproduce through manager-owned editor process when possible.
2. Capture current build identity (`lxe-editor-build-sync`) and process status
   (`lxe-manager-ops`) before and after the action.
3. Read `ops.editor_logs` immediately after an editor exit or failed command.
4. If logs are empty or unavailable, add targeted logging or manager/editor
   log capture first, deploy that observability change, then reproduce again.
5. Only propose or implement a behavioral fix after logs, command output,
   stack/error text, or a focused regression test identifies the failing layer.

If a command returns success and the editor exits on the next tick, treat the
boundary between command handling, deferred apply, and renderer/runtime update
as unknown until logs or instrumentation prove where it failed.

## Guardrails

- Prefer MCP over direct file scraping once the server is connected.
- Do not say "likely", "probably", or equivalent when naming a root cause
  unless the evidence has been collected and stated.
- Do not call `recording_*`, `ops.*`, `editor.get_build_info`, or
  `lxe_editor_get_build_info` from this skill; switch to the focused skill.
- Do not maintain a second debug protocol in the skill.
- When a missing capability looks command-like, switch to
  `lxe-editor-command-reference` before proposing a new MCP tool.
- If tools or resources are missing, distinguish between:
  - repo-local manager registration missing
  - `data/lxe_editor/runtime_state.yaml` missing for editor HTTP/WS discovery
  - configured manager MCP URL not responding yet
  - manager server connected but missing a specific `lxe_editor_*` tool or
    `lxe-editor://` resource
- Keep resource names and tool names aligned with the approved design:
  `lxe_editor_*` for tools, `lxe-editor://...` for resources.

## Typical Debug Sequence

1. Ensure the editor is running.
2. Read `lxe-editor://summary`.
3. Load the intended scene if summary shows no scene/document for the current
   task.
4. Read `lxe-editor://selection` or `lxe-editor://scene` if the issue is about
   picking, selection, or document state.
5. Issue one command or pick action.
6. Re-read the affected resource or wait for the expected state.
