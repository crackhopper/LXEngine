---
name: lxe-verify-implement
description: Use when verifying LXEngine implementation changes in the remote lxe_editor after local commits, especially when code must be pushed, pulled, rebuilt, restarted, exercised through editor commands, and visually checked.
---

# lxe_verify_implement

Use this skill to prove a local LXEngine implementation is present in the remote
`lxe_editor` runtime, builds there, launches, loads a scene, survives basic
editor commands, and leaves the user with a concrete visual check to perform.

This skill composes these existing skills when needed:

- `lxe-manager-ops` for process status, pull, build, logs, start/restart.
- `lxe-debug` for build identity checks, command syntax lookup,
  summary/camera/selection reads, and small commands.
- `lxe-use-case-runner` for recorded or multi-step editor scenarios.
- `lxe-recording` for recording/replay artifacts.

## Preconditions

Before touching the remote editor:

1. Run `git status --short`.
2. Confirm local changes are committed if they must be deployed remotely.
3. Check the current branch and target remote:
   - `git branch --show-current`
   - `git status -sb`
   - `git remote -v`
4. Decide the deployment target:
   - If the manager checkout tracks `main`, push a fast-forward update to
     `main` only when the user has approved integration or the task requires
     remote verification on main.
   - Otherwise push the feature branch and ensure the manager checkout can pull
     that branch.

Do not use destructive git commands. Do not overwrite unrelated user work.

## Remote Update And Build

1. Stop the running editor before updating code:
   - `ops.editor_status`
   - `ops.editor_stop` when running
2. Push committed local work if the remote cannot see it:
   - Feature branch: `git push -u origin <branch>`
   - Main fast-forward: first prove `origin/main` is an ancestor of `HEAD`, then
     `git push origin HEAD:main`
3. Pull on the manager side:
   - `ops.repo_pull`
4. Confirm the manager checkout/build identity:
   - `ops.build_state`
   - Compare `repoHeadShort` with the expected local commit.
5. Build the smallest target that proves the change:
   - Usually `ops.build_target` for `lxe_editor`.
   - Use `ops.build_configure` first only when configure inputs changed or the
     build output shows CMake state is stale.

If pull reports “Already up to date” but `repoHeadShort` is older than the
expected commit, the manager is on a different branch. Resolve that branch
alignment before trusting the build.

## Start And Load Scene

1. Start or restart the editor:
   - `ops.editor_start` or `ops.editor_restart`
   - `lxe_editor_ensure_running`
2. Read the current state:
   - `lxe_editor_get_summary`
   - `lxe_editor_get_cameras`
3. If no intended scene is loaded, verify command syntax with `lxe-debug`, then
   load a scene through command bus:

```text
scene load assets/scenes/lxe_editor.scene.yaml
```

For retained Helmet scene verification, prefer:

```text
scene load assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
```

When the user says “上一个场景” or “previous scene”, first check summary for the
active project and scene. If it already restored a project scene, do not replace
it unless the test requires a known fixture.

## Command Smoke Sequence

Keep commands small and reversible. Verify after each command with summary,
cameras, selection, or wait-for.

Recommended baseline:

```text
preview off
deselect
cam control orbit
cam look-at 6.0 4.0 8.0 0.0 1.0 0.0
```

Scene-authoring smoke when appropriate:

```text
select /dir_light
set /dir_light.light.shadowStrength 0.75
set /dir_light.light.shadowDistance 80
set /dir_light.light.shadowCascadeCount 4
```

Only use node paths that exist in the loaded scene. Read scene/selection first
or use a known fixture scene before issuing path-specific commands.

## Implementation-Specific Checks

Choose checks that match the change.

| Change type | Evidence to collect |
|---|---|
| FrameGraph / render target | Build state at expected commit; editor starts; scene load succeeds; no FrameGraph compile/runtime error in logs |
| Shadow / CSM | Shader compilation includes `shadow_depth_only`; editor starts with a shadow scene; summary is healthy; user can see caster shadow on receiver |
| Editor command/UI | Command returns success; summary/selection/camera state changes as expected |
| Scene serialization | Load target scene, save if requested, reload, then re-read summary/scene state |
| Notes only | `scripts/notes/serve_site.sh --build`; restart notes site if preview is stale |

For renderer visual features, MCP state is necessary but not sufficient. Always
tell the user what to inspect visually in the editor window.

## User Visual Check Prompt

End remote visual verification with a concrete prompt, for example:

```text
请在 editor 里看 realtime_offline_compare_helmet_pbr 场景：
- Damaged Helmet 应加载出来。
- 材质应走 lxe.material.v2 PBRT envelope。
- 默认 runtime 不应加载旧 Blinn-Phong/debug/RTR 材质。
```

Report:

- Expected commit and manager `repoHeadShort`.
- Build command/result.
- Editor PID and scene loaded/restored.
- Commands run and observed state.
- Logs checked, especially any renderer/shader/FrameGraph errors.
- What still requires human visual confirmation.
