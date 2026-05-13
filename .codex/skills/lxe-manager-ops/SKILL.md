---
name: lxe-manager-ops
description: Use when querying lxe_editor process status through lxe_manager, operating managed processes, repository pull, CMake configure/build, logs, or resource-guardian failures.
---

# lxe-manager-ops

Use this skill for checking whether `lxe_editor` is running and for operations
that can start, stop, update, or rebuild the editor environment. Keep diagnostic
probing in `lxe-editor-debug` and build comparison in `lxe-editor-build-sync`.

## Tools

| Tool | Use |
|---|---|
| `ops.editor_status` | Check managed editor process state |
| `ops.editor_start` / `ops.editor_stop` | Start or stop the managed editor |
| `ops.editor_logs` | Inspect manager/editor logs |
| `ops.repo_pull` | Run repository `git pull --ff-only` through manager |
| `ops.build_configure` | Run CMake configure |
| `ops.build_target` | Build a specific CMake target |
| `lxe_editor_ensure_running` | Non-destructive editor health check |

## Status Query

Use `ops.editor_status` when the question is whether the manager has a running
editor process. This works even when editor HTTP discovery is unavailable.

Interpret common results:

| Result | Meaning | Next step |
|---|---|---|
| `{ "running": false }` | Manager is reachable, but it has no managed editor process | Report editor is not started, or use `ops.editor_start` if startup was requested |
| `lxe_editor_ensure_running` returns `editor_unavailable` | Manager cannot discover editor HTTP API via `runtime_state.yaml` and token file | Check `ops.editor_status` before assuming an MCP or token problem |
| `ops.editor_status` shows running but `lxe_editor_ensure_running` fails | Process exists, but HTTP discovery/health is missing or stale | Read `ops.editor_logs` and report the mismatch |

## Workflow

1. Check `ops.editor_status` before mutating process state or debugging
   `editor_unavailable`.
2. Stop the editor before pull/build when the running binary may lock files or
   use stale assets.
3. Pull with `ops.repo_pull` only after confirming no unrelated local edits will
   be overwritten.
4. Configure before build when CMake inputs, build identity, or dependencies
   changed.
5. Build the smallest target that satisfies the task, usually `lxe_editor`.
6. Start the editor and verify with `lxe_editor_ensure_running`.

## Resource Guardian Failures

If manager reports CPU, memory, or IO guard termination:

- Treat the killed operation as failed even if partial logs look successful.
- Read `ops.editor_logs` or status before retrying.
- Retry with a narrower target or lower concurrency when build pressure caused
  the termination.
- Report the guard reason and affected process; do not hide it as a generic
  timeout.

## Guardrails

- Do not use destructive Git commands.
- Do not restart a user-controlled editor unless the user approved that flow or
  build-sync showed it is required.
- Do not inspect command syntax here; use `lxe-editor-command-reference`.
- Do not expand manager MCP tools for editor-local actions until checking
  whether the command console can expose the capability through
  `lxe_editor_command`. Prefer command-bus expansion when it avoids restarting
  the manager server.
- After local fixes that must be deployed remotely, use
  `lxe-remote-fix-rebuild-retest` for the full stop/pull/build/start/retest
  workflow.
