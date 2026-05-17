---
name: lxe-remote-refresh-restore
description: Use when the MCP-managed remote lxe_editor must preserve the current scene, stop, pull latest code, build, restart, and reload the saved scene.
---

# lxe_remote_refresh_restore

Use this skill when the user asks to update or relaunch the remote editor while
preserving the current scene state.

Coordinate with:

- `lxe-manager-ops` for process, pull, build, and start operations.
- `lxe-debug` command syntax lookup before issuing scene save/load commands.
- `lxe-debug` for state checks after restart.

## Required Sequence

1. Read `ops.editor_status` and `lxe_editor_get_summary`.
2. Choose a restore scene path. Prefer the current document path when present.
   If the current document path is empty or protected, save to an explicit local
   restore path such as:

   ```text
   data/scenes/codex_restore_after_remote_update.scene.yaml
   ```

   Use repo-relative paths with forward slashes for command-bus scene paths.
   Do not copy a Windows `currentDocumentPath` with backslashes directly into
   `scene save` / `scene load`; backslashes are command-parser escapes and can
   change the path token.

3. Save the scene with:

   ```text
   scene save <restore-path>
   ```

4. Verify the save succeeded. Re-read summary and confirm `dirty` is false or
   that `currentDocumentPath` reports the restore path.
5. Stop the editor with `ops.editor_stop`.
6. Pull latest code with `ops.repo_pull`.
7. Build `lxe_editor` with `ops.build_target`.
8. Start the editor with `ops.editor_start`.
9. Verify health with `lxe_editor_ensure_running`.
10. Restore the saved scene with:

    ```text
    scene load <restore-path>
    ```

11. Verify the restored scene by reading `lxe_editor_get_summary`; confirm the
    document path/source and selection/camera state as needed for the task.

## Guardrails

- Do not stop the editor if scene save failed and the scene is dirty.
- Do not assume `scene save` succeeded from command dispatch alone; verify state.
- If the editor command endpoint is unavailable but state GET endpoints work,
  report the POST/command failure and keep the editor running.
- If local fixes must be included in the remote pull, commit and push them
  before `ops.repo_pull`.
- Do not run destructive Git commands.
- Treat resource-guardian kills or build failures as hard failures; inspect
  `ops.editor_logs` before retrying.
