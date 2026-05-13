# Editor Recording and Replay Design

## Goal

Build a debug-first recording and replay system for `lxe_editor`. The system
should let a user record a problematic editor interaction, save it as a
self-contained file, and let an agent replay or inspect it through
`lxe_manager` MCP.

The first version prioritizes practical bug reproduction. It does not attempt
frame-perfect input replay or deterministic game-style simulation.

## Scope

In scope:

- Record user UI operations inside `lxe_editor`.
- Record MCP-originated operations into the same session with a source marker.
- Keep active recording data in editor memory while a session is running.
- Save completed recordings under `data/lxe_editor/recordings/`.
- Replay recordings step by step and stop on the first failure.
- Expose recording control, replay, and probe operations through `lxe_manager`
  MCP.

Out of scope for the first version:

- Full SDL event stream capture.
- Frame-perfect mouse motion replay.
- CI/headless deterministic replay.
- Video capture or GPU frame capture.

## Architecture

The recorder lives in the editor, not in `lxe_manager`. The editor owns the
real command bus, toolbar state, selection state, camera input adapters, and
scene runtime. `lxe_manager` only exposes remote control and artifact access.

| Component | Responsibility |
|---|---|
| `RecordingController` | Global enable/disable, current session lifecycle, detail level, status |
| `RecordingSession` | In-memory steps, snapshots, errors, metadata, save path |
| `RecordingSinks` | Small hooks at command, toolbar, selection/pick, camera/input, and MCP entry points |
| `ReplayRunner` | Loads a recording and executes steps in order |
| `MCP recording tools` | Start/stop/list/read/replay/status/probe from Codex |

## Recording State Model

Recording has two gates:

| Gate | Meaning |
|---|---|
| `enabled` | Recorder hooks may run. Default is disabled. |
| `active session` | A recording is currently collecting steps. |

When disabled, hooks must return after a cheap branch. They must not allocate
large payloads, serialize JSON, capture snapshots, or touch the filesystem.

When enabled but no session is active, hooks may expose status but must not
append steps.

## Detail Levels

The file format reserves detail levels from the start so later versions can
add heavier diagnostics without breaking recordings.

| Level | First-version behavior |
|---|---|
| `basic` | Record semantic steps and minimal metadata. |
| `diagnostic` | Record semantic steps plus summary, toolbar, selection, and cameras around important steps. |
| `trace` | Reserved for future high-cost dumps such as input queues, command-history diffs, event traces, and performance data. |

The default should be `basic`.

## Step Model

The canonical recording step is semantic, not raw input. Raw-like inputs are
compressed into useful debug actions.

Common fields:

| Field | Meaning |
|---|---|
| `id` | Monotonic step id inside the recording |
| `kind` | Step type |
| `source` | `user_ui`, `mcp`, or `system` |
| `timeOffsetMs` | Milliseconds since session start |
| `payload` | Step-specific data |
| `beforeSnapshotId` | Optional snapshot reference |
| `afterSnapshotId` | Optional snapshot reference |

Initial step kinds:

| Kind | Payload |
|---|---|
| `command` | `{ "line": "scene load lxe_editor.scene.yaml" }` |
| `toolbar_change` | `{ "field": "camera", "value": "freefly" }` |
| `pick` | `{ "viewportX": 512, "viewportY": 320 }` |
| `selection_change` | `{ "paths": ["/helmet"] }` |
| `camera_mode_change` | `{ "mode": "freefly" }` |
| `hold_key` | `{ "key": "W", "durationMs": 500 }` |
| `mouse_button` | `{ "button": "right", "action": "down" }` |
| `mouse_drag` | `{ "button": "left", "from": [100, 200], "to": [180, 260], "durationMs": 350 }` |

Mouse drag records start and end only. The first version should not record every
intermediate mouse move.

## Recording File

Recordings are JSON files saved under:

```text
data/lxe_editor/recordings/<timestamp>-<short-id>.json
```

Top-level shape:

```json
{
  "schemaVersion": 1,
  "metadata": {
    "startedAt": "2026-05-13T00:00:00Z",
    "editorVersion": "unknown",
    "platform": "windows",
    "scenePath": "assets/scenes/lxe_editor.scene.yaml",
    "window": { "width": 1600, "height": 900 }
  },
  "detailLevel": "basic",
  "steps": [],
  "snapshots": {},
  "errors": []
}
```

The format is intentionally readable so it can be analyzed offline without
starting the editor.

## Replay Behavior

Replay executes steps in order and stops on the first failure. The failure
result must include:

- recording id or path
- failed step id
- failed step kind
- error message
- number of completed steps
- current summary, selection, toolbar, and cameras when available

This preserves the failing editor state for follow-up MCP probes.

Replay should wait for editor state to stabilize after operations that are
known to be asynchronous, such as scene loading.

## MCP Surface

`lxe_manager` should expose recording tools that forward to the editor HTTP API.

| Tool | Purpose |
|---|---|
| `recording_enable` | Enable recorder hooks |
| `recording_disable` | Disable recorder hooks and reject active recording unless forced |
| `recording_status` | Return enabled/session/detail/save state |
| `recording_start` | Start an active session with a detail level |
| `recording_stop` | Stop the session and optionally save |
| `recording_list` | List saved recordings |
| `recording_read` | Read a saved recording or active session |
| `recording_replay` | Replay a recording, default `stopOnFailure: true` |
| `recording_probe` | Read summary, selection, cameras, toolbar, or scene during analysis |

The existing `lxe_editor_*` tools remain the preferred low-level probes.
Recording tools add lifecycle and artifact management.

## Error Handling

- Starting a session while disabled returns a clear error.
- Starting a second session returns the current active session id.
- Disabling while active requires an explicit force flag or fails.
- Save failures keep the in-memory session available until the next explicit
  discard.
- Replay failure stops immediately and leaves the editor in the failure state.

## Testing Strategy

Unit tests should cover:

- disabled recorder has no side effects
- session start/stop transitions
- step append with source and time offset
- basic and diagnostic snapshot policy
- JSON save/load round trip
- replay stop-on-first-failure behavior

Integration tests should cover:

- command bus operation records a `command` step
- toolbar camera mode change records a semantic step
- MCP-originated command records `source: "mcp"`
- replay of a small scene-load recording reaches the expected summary

## Open Constraints

- The first version should avoid raw SDL event replay because window focus,
  frame timing, and ImGui capture can make it unstable.
- The design must preserve the current editor input model, including freefly
  requiring right mouse held for WASD movement.
- Heavy diagnostic dumps belong behind detail levels and must not run while
  disabled.
