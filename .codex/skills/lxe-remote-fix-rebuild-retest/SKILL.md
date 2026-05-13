---
name: lxe-remote-fix-rebuild-retest
description: Use after fixing lxe_editor or lxe_manager code locally when the remote manager should pull, build, restart editor, and rerun the MCP reproduction.
---

# lxe-remote-fix-rebuild-retest

Use this as the top-level workflow skill after a local code fix has been
committed and pushed, when a remote MCP-managed environment must be updated
before the reproduction can continue. This skill coordinates focused skills; it
does not replace them.

By default Codex should run this workflow by itself through MCP. Ask the user
for help only when the manager MCP server itself must be restarted or when MCP
is unreachable.

## Required Sequence

1. Verify local tests for the fix.
2. Commit and push the fix.
3. Use `lxe-manager-ops` to call `ops.editor_status`.
4. Stop editor with `ops.editor_stop` if it is running.
5. Pull remote code with `ops.repo_pull`.
6. Configure only when CMake inputs or dependencies changed.
7. Build the smallest needed target with `ops.build_target`, usually
   `lxe_editor`.
8. Read `ops.build_state` when available; it is the manager-side record of the
   most recent successful build action and does not require CMake reconfigure to
   update Git identity.
9. Start editor with `ops.editor_start` or restart with `ops.editor_restart`
   when the manager already has a running editor.
10. Verify health with `lxe_editor_ensure_running`.
11. Use `lxe-editor-build-sync` for editor-reported identity when relevant, but
   prefer `ops.build_state` for the last manager build action.
12. Return to the original reproduction skill, such as `lxe-editor-recording`,
    and rerun the full scenario.

## MCP Server Restart Boundary

If the local fix changes `tools/lxe_manager/src/mcp/server.ts`, CLI parsing,
manager ops tool registration, or any code that defines MCP tool names, the
already-running manager cannot hot-load the change. In that case:

- Push the fix.
- Tell the user to restart the MCP server process.
- After restart, continue this workflow from `ops.editor_status`.

If the fix only changes editor code or command-bus behavior, do not ask for user
help; use MCP to stop, pull, build, start, and retest.

## Command Console Expansion

If the missing capability is editor-local, prefer adding a command-bus command
and using `lxe_editor_command` rather than expanding manager MCP protocol. Use
`lxe-editor-command-reference` to verify syntax and document the new command.

## Guardrails

- Do not use destructive Git commands.
- Do not skip stop/pull/build/start when the remote binary is known stale.
- Do not run CMake configure solely to refresh editor compile-time Git macros;
  use `ops.build_state` for manager-side build identity.
- Treat resource-guardian kills as failed builds or failed starts; inspect
  `ops.editor_logs` before retrying.
- The workflow is only complete after the original MCP reproduction passes on
  the rebuilt remote editor.
