---
name: lxe-debug
description: Use when diagnosing user-visible lxe_editor problems through lxe_manager MCP, including scene state, command behavior, picking, camera, crashes, stale builds, logs, or command-bus syntax.
---

# lxe_debug

Use this as the main MCP debugging entry for user-reported `lxe_editor`
problems. It combines state inspection, small command-bus probes, command syntax
lookup, build identity checks, and evidence collection. Process lifecycle and
remote build operations still belong to `lxe-manager-ops`.

## First Triage

| User symptom | First evidence |
|---|---|
| Editor not responding | `lxe_editor_ensure_running`, then `ops.editor_status` if unavailable |
| Scene looks wrong | `lxe_editor_get_summary`, `lxe_editor_get_cameras`, selection/scene resources |
| Command failed | Exact `lxe_editor_command` result, then command registration search |
| Editor exited or froze | `ops.editor_status`, `ops.editor_logs`, last command sent |
| Recent code not visible | editor/build identity if available, `ops.build_state`, local `git rev-parse HEAD` |
| Picking/selection issue | summary, selection, camera, then `lxe_editor_pick` |

Never diagnose from the symptom alone. State what is proven, what is unknown,
and what evidence will be collected next.

## MCP Surfaces

Prefer stable state reads before actions:

- `lxe_editor_get_summary`
- `lxe_editor_get_selection`
- `lxe_editor_get_cameras`
- `lxe_editor_wait_for`
- MCP resources when available: `lxe-editor://summary`,
  `lxe-editor://selection`, `lxe-editor://cameras`, `lxe-editor://scene`

Use these for small probes:

- `lxe_editor_command`
- `lxe_editor_pick`

Use manager ops only for lifecycle evidence or handoff:

- `ops.editor_status`
- `ops.editor_logs`
- `ops.build_state`

## Debug Workflow

1. Confirm MCP/editor availability:
   - Call `lxe_editor_ensure_running`.
   - If it returns `editor_unavailable`, call `ops.editor_status`.
   - If a process exists but HTTP discovery fails, read `ops.editor_logs`.
2. Anchor current state:
   - Read summary first.
   - Read cameras and selection when the problem involves view, picking, or
     object state.
3. Reproduce minimally:
   - Prefer one command or one pick at a time.
   - Re-read state after each action.
   - If the editor exits after a command, capture status and logs immediately.
4. Check build freshness only when relevant:
   - Compare editor build info if the MCP surface exposes it.
   - Otherwise use `ops.build_state` plus local `git rev-parse HEAD`.
   - If stale, stop debugging and switch to `lxe-manager-ops` or
     `lxe-verify-implement`.
5. Report with evidence:
   - Initial state.
   - Exact command/pick used.
   - Observed state/log change.
   - Proven failure layer or remaining unknowns.

## Command Syntax Lookup

Do not guess command names from UI labels. For any non-trivial
`lxe_editor_command`:

1. Identify the intended action in plain language.
2. Search current code:

```bash
rg -n "register.*command|Command|executeCommand|command bus|lxe_editor_command" src/demos/lxe_editor src/core src/infra
```

3. Extract exact command name, argument order, valid values, and side effects.
4. Use canonical payload field `line`; `command` is only a compatibility alias.
5. Prefer repo-relative paths with forward slashes for scene paths.

Common safe commands:

```text
preview off
deselect
cam control orbit
cam look-at 6.0 4.0 8.0 0.0 1.0 0.0
scene load assets/scenes/lxe_editor.scene.yaml
scene load assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
```

With preview off and no selected camera node, `cam look-at` changes
`editor_cam`. Run `deselect` before pick-heavy probes so a previously selected
`/game_cam` does not receive camera edits.

## Build Identity Checks

Use build identity checks to decide whether a remote rebuild is needed, not as a
substitute for debugging.

| Evidence | Meaning |
|---|---|
| Editor build info matches local `HEAD` | Continue debugging behavior |
| `ops.build_state.repoHeadShort` matches expected commit | Last manager build used expected source |
| Build info unavailable but editor running | Report surface gap; use `ops.build_state` as best manager-side evidence |
| Remote head older than local fix | Switch to `lxe-verify-implement` |

Do not run pull/build/start from `lxe-debug`; hand off to `lxe-manager-ops` or
`lxe-verify-implement`.

## User Problem Patterns

### Scene load or restore looks wrong

1. Read summary and current project/active scene.
2. If a specific scene is expected, verify command syntax and path.
3. Run `scene load ...` only after confirming it is safe to replace current
   state or the user requested it.
4. Re-read summary and scene state.

### Selection or picking is wrong

1. Read summary, cameras, selection.
2. Use `deselect` if stale selection may affect the probe.
3. Use `lxe_editor_pick` with explicit coordinates.
4. Re-read selection and last hit data.

### Rendering feature needs verification

1. Prove code/build/editor state first.
2. Load a known fixture scene when available.
3. Run small camera commands to frame the feature.
4. Check logs for renderer/shader/FrameGraph errors.
5. Tell the user exactly what must be visually inspected. MCP state is not
   pixel-level visual proof unless a screenshot/pixel tool is available.

### Crash or unexpected exit

1. Capture last command/action.
2. Call `ops.editor_status`.
3. Read `ops.editor_logs`.
4. Do not restart until logs/status are captured.
5. If logs are empty, improve logging or reproduce under manager before fixing.

## Guardrails

- Prefer MCP over direct file scraping once editor API is available.
- Do not use destructive Git commands.
- Do not send broad command sequences without checking state between steps.
- Do not expand manager MCP protocol for editor-local behavior until checking
  whether command bus can express the operation.
- If a root cause is not proven, report it as unknown and list the next
  evidence step.
