---
name: lxe-editor-recording
description: Use when recording, reading, replaying, or probing lxe_editor interaction recordings through lxe_manager MCP.
---

# lxe-editor-recording

Use this skill only for recording artifacts and replay-based debugging. Ordinary
state reads belong to `lxe-editor-debug`; process lifecycle work belongs to
`lxe-manager-ops`.

## Tools

| Tool | Use |
|---|---|
| `recording_status` | Check enabled/session/detail/save state |
| `recording_enable` / `recording_disable` | Turn recorder hooks on or off |
| `recording_start` / `recording_stop` | Start or end one recording session |
| `recording_list` / `recording_read` | Find and inspect saved or active JSON recordings |
| `recording_replay` | Replay a recording and stop at the first failure |
| `recording_probe` | Read focused state while analyzing a recording |

## Workflow

1. Call `recording_status` before changing anything.
2. Confirm the editor has a loaded scene before starting a recording. Use
   `lxe-editor-debug` to read `lxe-editor://summary` or `lxe_editor_get_summary`;
   if no scene/document is loaded, load the intended test scene first.
3. Enable recording only when needed; it is intentionally off by default.
4. Start with `detailLevel: "basic"` unless the user asks for heavier dumps.
5. Stop with save enabled when the recording should become a bug artifact.
6. Read the saved JSON before replaying; note step ids, sources, and payloads.
7. Replay once, then use `recording_probe` at the failure state instead of
   repeatedly mutating the editor.

If `recording_status` returns `editor_unavailable`, stop the recording workflow
and switch to `lxe-manager-ops` to call `ops.editor_status`. That distinguishes
"manager reachable but editor not started" from recording API failure.

Do not use recording as the first operation after starting editor. A recording
without a loaded scene is usually not useful and can hide setup bugs as replay
bugs.

## Guardrails

- Treat recordings as debug artifacts, not deterministic frame-perfect tests.
- Do not record broad diagnostic or trace data unless the user accepts the cost.
- If replay fails, report recording id/path, failed step id, completed step
  count, and the probe target used for follow-up.
- Do not use this skill for command syntax discovery; load
  `lxe-editor-command-reference` only when command payloads need verification.
- If testing reveals a code defect, fix it locally and use
  `lxe-remote-fix-rebuild-retest` to push, pull, rebuild, restart, and rerun the
  recording scenario.
