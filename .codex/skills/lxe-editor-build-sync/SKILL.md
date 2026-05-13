---
name: lxe-editor-build-sync
description: Use when comparing the running lxe_editor build identity with local Git state before deciding whether manager ops are needed.
---

# lxe-editor-build-sync

Use this skill to answer one question: is the editor binary Codex is controlling
built from the source state we expect? It should expose facts and route to ops
only when a rebuild/restart is justified.

## MCP Tools

- `editor.get_build_info`
- `lxe_editor_get_build_info`

Both should return editor build identity such as `gitCommit`, `gitCommitShort`,
`gitDirty`, `buildType`, and optional `builtAt`.

## Local Checks

Use normal Git commands in the repo root:

```bash
git rev-parse HEAD
git status --short
```

## Decision Flow

1. Read editor build info through MCP.
2. Read local `HEAD` and dirty state.
3. Compare full commit hashes when available; use short hash only for display.
4. If commits match and local/editor dirty state is acceptable, continue with
   `lxe-editor-debug` or `lxe-editor-recording`.
5. If the editor is older, unknown, dirty unexpectedly, or built from a
   different commit, report the mismatch and switch to `lxe-manager-ops` for
   stop/pull/build/start.

## Guardrails

- Do not run `git pull`, build, stop, or start from this skill.
- Do not assume a remote editor is stale without both MCP build info and local
  Git evidence.
- If build info tools are missing, say the manager/editor surface is incomplete;
  do not infer the editor commit from local files.
