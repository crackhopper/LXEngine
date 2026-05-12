---
name: lxe-editor-command
description: Use when a user wants to control a running local or remote lxe_editor through natural-language instructions instead of spelling out MCP calls or command-bus text.
---

Use this skill together with `lxe-editor-debug` when the user describes an
desired editor action in plain language and expects Codex to translate that
intent into the correct `lxe_editor_*` MCP call sequence.

## Scope

This skill is for remote control, not source inspection.

- Read editor state through `lxe-editor://...` resources or `lxe_editor_get_*`
  tools.
- Execute mutations through `lxe_editor_command`.
- Use `lxe_editor_pick` only when the request is explicitly screen-coordinate
  driven.
- Verify every mutation with a follow-up read or `lxe_editor_wait_for`.

If the MCP server is unavailable, fall back to the blocker reporting rules from
`lxe-editor-debug`. Do not invent a second transport.

## Workflow

1. Load `lxe-editor-debug` first and confirm the `lxe_editor` MCP server is
   reachable.
2. Read `lxe-editor://summary` before acting.
3. Read narrower state only when needed:
   - selection-sensitive action: `lxe-editor://selection`
   - camera / preview action: `lxe-editor://cameras` or `lxe-editor://toolbar`
   - path / scene inspection: `lxe-editor://scene`
4. Translate the user's request into the smallest correct MCP action:
   - read-only question -> resource read
   - scene mutation expressible as command bus text -> `lxe_editor_command`
   - viewport click / probe by pixel coordinates -> `lxe_editor_pick`
5. After the action, verify the expected state change.

## Translation Rules

Prefer direct command-bus text only when the intent is unambiguous.

- Selection:
  - "select cube" -> resolve or inspect path first, then `select /path`
  - "select these nodes" -> one `select /a /b /c`
  - "clear selection" -> `deselect /`
- Transform:
  - "move selected object by 1 0 0" -> `move /path 1 0 0` or multi-target form
  - "rotate X by 90 degrees" -> `rotate /path 90 0 0`
  - "scale it to 2 2 2" -> `set /path.scale 2 2 2` if absolute intent is clear,
    otherwise ask or inspect before choosing between `scale` and `set`
- Camera / preview:
  - "turn preview on" -> `preview on`
  - "toggle preview" -> `preview toggle`
  - "set active camera fov to 75" -> `cam fov 75`
- Generic property writes:
  - "rename cube to hero" -> `set /path.name hero`
  - "set camera near plane to 0.5" -> `set /camera.near 0.5`
  - "switch camera to orthographic" -> `set /camera.projection orthographic`
- Scene structure:
  - "add a camera named debug_cam under /world" -> `select /world`, then
    `add camera debug_cam`
  - "remove /world/cube" -> `remove /world/cube`

## Natural-Language Patterns

Map the user's wording to editor intent before choosing the MCP action.

- Chinese selection phrases:
  - "选中 cube" / "帮我选中 cube" -> inspect path, then `select /path`
  - "取消选择" / "清空选择" -> `deselect /`
  - "选中这两个节点" -> inspect the two paths, then `select /a /b`
- Chinese transform phrases:
  - "把 cube 往右移 1 米" -> resolve axis in scene terms, then
    `move /path 1 0 0`
  - "向上旋转 90 度" -> disambiguate axis first, then `rotate /path ...`
  - "缩放到 2 倍" -> prefer absolute write only if uniform absolute scale is
    explicit; otherwise ask whether the user means relative `scale` or
    absolute `set /path.scale`
- Chinese camera / preview phrases:
  - "打开预览" / "进入预览模式" -> `preview on`
  - "关闭预览" -> `preview off`
  - "切换预览" -> `preview toggle`
  - "把视角 FOV 改成 60" -> `cam fov 60`
- Chinese property phrases:
  - "把 cube 改名成 hero" -> `set /path.name hero`
  - "把主相机近裁面改成 0.3" -> `set /camera.near 0.3`
  - "切到正交相机" -> `set /camera.projection orthographic`
- English path-light phrases:
  - "select the cube under world" -> prefer `/world/cube`
  - "rename the selected node to hero" -> inspect selection first, then
    `set /path.name hero`
  - "delete the selected camera" -> inspect selection first, then `remove /path`

## Ambiguity Handling

Resolve these ambiguities before mutating state:

- Relative vs absolute:
  - "move by 1" is relative -> `move`
  - "set position to 1 2 3" is absolute -> `set /path.translation 1 2 3`
  - "scale to 2" is ambiguous unless the user clearly means absolute scale
- Target scope:
  - "the selected object" -> read `lxe-editor://selection` first
  - "main camera" / "editor camera" / "game camera" -> confirm from
    `lxe-editor://cameras`
  - bare names like "cube" -> inspect scene/path state first
- Axis wording:
  - "rotate upward" / "turn left" are camera- or object-relative phrases;
    do not map them to XYZ blindly
- Structural intent:
  - "add a camera to world" -> select parent first if `add camera` is parented
    by current selection

## MCP Action Selection

Choose the narrowest surface that can satisfy the request.

- Read-only:
  - "what is selected?" -> `lxe-editor://selection`
  - "which camera is active?" -> `lxe-editor://cameras` or summary
  - "is preview on?" -> `lxe-editor://toolbar` or summary
- Write via command bus:
  - selection, transform, preview, rename, property changes, add/remove
- Pick:
  - "click the object at x=320 y=240"
  - "probe what is under the cursor"
- Wait/poll:
  - asynchronous scene loads
  - actions whose visible effect is confirmed by a later state snapshot

## Standard Execution Template

Use this sequence for every remote-control request:

1. Restate the intent in one sentence.
2. Read the minimum state needed to remove ambiguity.
3. Emit one MCP action.
4. Verify the expected result.
5. Report both the MCP action and the verified new state.

Example:

- User: "帮我打开预览模式"
- Read: `lxe-editor://summary`
- Action: `lxe_editor_command` with `preview on`
- Verify: `lxe-editor://toolbar`
- Report: preview enabled = true

Example:

- User: "rename the selected node to hero"
- Read: `lxe-editor://selection`
- Action: `lxe_editor_command` with `set /resolved/path.name hero`
- Verify: `lxe-editor://selection` or `lxe-editor://scene`
- Report: renamed `/resolved/path` to `hero`

## Guardrails

- Do not guess node paths when the scene state is available. Inspect first.
- Do not emit multiple commands when one command-bus line can express the
  action.
- Do not use `pick` unless the request references screen position, viewport
  coordinates, or click-like intent.
- When the request is ambiguous between relative and absolute edits, resolve
  that ambiguity before mutating state.
- Prefer command-bus operations that preserve undo/redo semantics.

## Result Reporting

After acting, report:

- the natural-language intent you applied
- the exact MCP action used
- the verified resulting state

If the request cannot be translated safely, report the missing detail instead
of issuing a guessed command.
