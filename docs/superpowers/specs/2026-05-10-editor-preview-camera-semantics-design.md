# Editor Preview Camera Semantics Design

Date: 2026-05-10

## Context

The current editor preview flow in `scene_viewer` mixes two separate concerns:

- `game_cam` is intended to be the scene's real gameplay camera.
- `editor_cam` is intended to be the editor-only working camera.

Today both cameras are initialized independently in `src/demos/scene_viewer/main.cpp`, which creates two problems:

1. The initial editor view does not match the gameplay camera.
2. The preview feature is hard to reason about because camera ownership and persistence semantics are unclear.

The desired behavior is:

- `game_cam` remains the persistent scene camera.
- `editor_cam` is an editor-only camera.
- Pressing `F` toggles which camera renders the main viewport.
- Toggling preview does not copy transforms between cameras.
- The editor camera should also persist, but only as editor metadata, not as a normal runtime scene node.

## Goals

- Make `game_cam` the authoritative scene camera.
- Make `editor_cam` the authoritative editor working camera.
- Ensure the first editor frame matches the gameplay camera when no prior editor state exists.
- Preserve editor camera pose across scene reopen through editor-only scene metadata.
- Keep preview switching as a pure active-camera toggle.

## Non-Goals

- No picture-in-picture preview.
- No dual rendering of editor and gameplay cameras in the same frame.
- No automatic sync from `editor_cam` back into `game_cam`.
- No persistence of transient UI state such as `previewEnabled`.

## Camera Roles

### `game_cam`

- Lives in the scene as normal runtime content.
- Participates in scene save/load.
- Represents the actual gameplay or authored camera state.
- Is the camera used when preview mode is enabled.

### `editor_cam`

- Exists only for editor interaction.
- Does not appear as a normal runtime scene node.
- Is restored from editor-only scene metadata when present.
- Is the camera used when preview mode is disabled.
- Remains attached to editor controls such as `CameraRig`.

## Initialization Rules

Scene load must follow this sequence:

1. Load or create `game_cam` as part of normal scene content.
2. Create `editor_cam` as the editor working camera.
3. If editor camera metadata exists, restore `editor_cam` from that metadata.
4. Otherwise, initialize `editor_cam` by copying the current `game_cam` state once.
5. Start with `previewEnabled = false`, so the initial active camera is `editor_cam`.

The fallback copy from `game_cam` to `editor_cam` must include:

- world position
- world rotation
- projection type
- `fovY`
- near plane
- far plane
- aspect ratio

Editor-only configuration remains specific to `editor_cam`, especially:

- culling mask includes `Layer_EditorOverlay`
- editor rig attachment and editor input ownership

## Preview Toggle Semantics

`preview on/off/toggle` and `F` must keep a single meaning:

- `preview on`: set the active camera to `game_cam`
- `preview off`: set the active camera to `editor_cam`
- `preview toggle`: switch between those two states

This action must not:

- copy pose from `editor_cam` to `game_cam`
- copy pose from `game_cam` to `editor_cam`
- modify camera projection settings

The only effect is switching which camera is marked active for rendering.

## Persistence Model

`editor_cam` state should be saved into an editor-only metadata section inside the scene file.

Recommended structure:

- a top-level `editor` block
- an `editorCamera` payload inside that block

The persisted `editorCamera` payload should include:

- position
- rotation
- projection type
- `fovY`
- near plane
- far plane

It should not include:

- preview enabled state
- active camera choice
- transient gizmo mode
- panel layout state unless that is explicitly designed later

This keeps runtime scene content and editor workspace state separate while allowing scene-scoped editor restoration.

## Runtime Behavior

Expected user-visible behavior:

1. On first open of an older scene with no editor metadata, the editor viewport starts from the same framing as `game_cam`.
2. If the user moves `editor_cam`, that changes only editor navigation.
3. Pressing `F` switches the whole rendered viewport to `game_cam`.
4. Pressing `F` again switches back to the current `editor_cam`.
5. Saving and reopening the scene restores the last editor camera view from metadata.

## Implementation Shape

This design implies three focused changes:

### 1. Demo / scene bootstrap

Update `src/demos/scene_viewer/main.cpp` so camera creation order and defaults match the new semantics:

- create `game_cam` first
- create `editor_cam` second
- restore `editor_cam` from metadata when available
- otherwise copy `game_cam` once

### 2. Editor camera state persistence

Add a scene-facing editor metadata contract that can serialize and deserialize editor camera state without introducing `editor_cam` into the runtime node list.

### 3. Verification coverage

Add automated checks for:

- initial fallback: `editor_cam` equals `game_cam` when metadata is absent
- preview toggle: active camera changes, but neither camera pose changes
- metadata restore: persisted editor pose comes back on reload

## Risks And Tradeoffs

### Why not sync on every preview toggle?

Because that would blur the boundary between authored scene state and editor workspace state. It would also make command history and future save semantics harder to reason about.

### Why not store `editor_cam` as a normal scene node?

Because it pollutes runtime scene content with editor-only state, complicates scene authoring rules, and makes runtime consumers pay attention to a camera they should not care about.

### Why store editor camera state in the scene file?

Because the desired experience is scene-scoped continuity. The same scene should reopen to the same editor framing regardless of machine or workspace, which a per-user local session file would not provide.

## Acceptance Criteria

- A scene with no editor metadata opens with `editor_cam` visually matching `game_cam`.
- `preview on` causes the main viewport to render from `game_cam`.
- `preview off` causes the main viewport to render from `editor_cam`.
- Preview toggling does not modify either camera's transform or projection settings.
- After moving the editor camera, saving, and reopening the scene, the editor viewport restores to the saved editor pose.
- Existing scenes without editor metadata continue to load through the fallback path without errors.
