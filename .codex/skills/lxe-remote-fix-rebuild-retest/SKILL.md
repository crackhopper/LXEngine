---
name: lxe-remote-fix-rebuild-retest
description: Use after fixing lxe_editor or lxe_manager code locally when the remote manager should pull, build, restart editor, and rerun the MCP reproduction.
---

# lxe-remote-fix-rebuild-retest

Use this skill after a local code fix has been committed and pushed, when the
running remote editor must be updated before the MCP reproduction can continue.
This is a workflow skill that coordinates focused skills; it does not replace
them.

## Required Sequence

1. Verify local tests for the fix.
2. Commit and push the fix.
3. Use `lxe-manager-ops` to call `ops.editor_status`.
4. Stop editor with `ops.editor_stop` if it is running.
5. Pull remote code with `ops.repo_pull`.
6. Configure only when CMake inputs or dependencies changed.
7. Build the smallest needed target with `ops.build_target`, usually
   `lxe_editor`.
8. Start editor with `ops.editor_start`.
9. Verify health with `lxe_editor_ensure_running`.
10. Use `lxe-editor-build-sync` to confirm remote build identity.
11. Return to the original reproduction skill, such as `lxe-editor-recording`,
    and rerun the full scenario.

## Command Console Expansion

If the missing capability is editor-local, prefer adding a command-bus command
and using `lxe_editor_command` rather than expanding manager MCP protocol. Use
`lxe-editor-command-reference` to verify syntax and document the new command.

## Guardrails

- Do not use destructive Git commands.
- Do not skip stop/pull/build/start when the remote binary is known stale.
- Treat resource-guardian kills as failed builds or failed starts; inspect
  `ops.editor_logs` before retrying.
- The workflow is only complete after the original MCP reproduction passes on
  the rebuilt remote editor.
