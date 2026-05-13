---
name: lxe-manager-ops
description: Use when operating lxe_manager-managed processes, repository pull, CMake configure/build, logs, or resource-guardian failures.
---

# lxe-manager-ops

Use this skill for operations that can start, stop, update, or rebuild the
editor environment. Keep diagnostic probing in `lxe-editor-debug` and build
comparison in `lxe-editor-build-sync`.

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

## Workflow

1. Check `ops.editor_status` before mutating process state.
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
