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
- The scene must support explicit serialize/deserialize so camera state is not tied to process startup only.
- Scene load/save must also be reachable from the command line inside the editor.

## Goals

- Make `game_cam` the authoritative scene camera.
- Make `editor_cam` the authoritative editor working camera.
- Ensure the first editor frame matches the gameplay camera when no prior editor state exists.
- Preserve editor camera pose across scene reopen through editor-only scene metadata.
- Keep preview switching as a pure active-camera toggle.
- Define a concrete scene document contract for runtime scene content plus editor metadata.
- Add command-bus entrypoints for manual scene load/save during an editor session.

## Non-Goals

- No picture-in-picture preview.
- No dual rendering of editor and gameplay cameras in the same frame.
- No automatic sync from `editor_cam` back into `game_cam`.
- No persistence of transient UI state such as `previewEnabled`.
- No attempt in this change to design a repo-wide generic asset pipeline for every scene type.

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

## Scene Serialization Contract

This change requires an explicit scene document instead of implicit hard-coded bootstrap state.

The document must support both:

- runtime scene content needed to rebuild the authored demo scene
- editor-only metadata needed to restore editor workspace state

Recommended top-level structure:

- `scene`
- `gameCamera`
- `editor`

Minimum required payloads for this change:

### `scene`

This block owns the authored scene facts that should survive save/load. For the scope of this design, it may stay narrow and only cover what `scene_viewer` already needs to reconstruct its default scene consistently.

At minimum it should identify:

- the scene name
- the default gameplay camera name or identity
- any demo-local authored defaults that are currently hard-coded in `main.cpp`

This design deliberately allows the first serializer/deserializer to stay `scene_viewer`-scoped instead of pretending a generic scene asset system already exists.

### `gameCamera`

This block owns the persistent authored gameplay camera state:

- position or eye/target/up representation
- rotation if stored as direct pose instead of look-at
- projection type
- `fovY`
- near plane
- far plane

### `editor`

This block owns editor-only metadata. For this change it only needs:

- `editorCamera`

The runtime loader must ignore the `editor` block when building gameplay scene content, while the editor bootstrap path must read it when restoring `editor_cam`.

## Scene Load/Save Commands

Scene persistence should not be reachable only through process startup and shutdown. The command bus must expose manual scene I/O so the editor can be driven from the console and later from MCP.

Recommended commands:

- `scene load <path>`
- `scene save`
- `scene save <path>`

Semantics:

- `scene load <path>`: deserialize the target scene document, rebuild runtime scene content, restore `game_cam`, restore `editor_cam` from editor metadata if present, otherwise fall back to `game_cam`.
- `scene save`: serialize the current scene back to the current scene document path.
- `scene save <path>`: serialize the current scene to an explicit path and treat that path as the current scene path for subsequent saves in the session.

Constraints:

- commands must return normal `CommandResult` values with stable human-readable messages
- failures stay in-band; no process exit on parse or file errors
- `structured` payload should include the resolved scene path and the action performed
- preview state itself is not serialized; after load the editor should return to normal editor mode with `editor_cam` active

## Runtime Behavior

Expected user-visible behavior:

1. On first open of an older scene with no editor metadata, the editor viewport starts from the same framing as `game_cam`.
2. If the user moves `editor_cam`, that changes only editor navigation.
3. Pressing `F` switches the whole rendered viewport to `game_cam`.
4. Pressing `F` again switches back to the current `editor_cam`.
5. Saving and reopening the scene restores the last editor camera view from metadata.
6. Running `scene load <path>` during an editor session rebuilds the loaded scene and resets camera routing according to the same bootstrap rules.
7. Running `scene save` or `scene save <path>` writes both gameplay camera state and editor camera metadata.

## Implementation Shape

This design implies five focused changes:

### 1. Demo / scene bootstrap

Update `src/demos/scene_viewer/main.cpp` so camera creation order and defaults match the new semantics:

- create `game_cam` first
- create `editor_cam` second
- restore `editor_cam` from metadata when available
- otherwise copy `game_cam` once

### 2. Editor camera state persistence

Add a scene-facing editor metadata contract that can serialize and deserialize editor camera state without introducing `editor_cam` into the runtime node list.

### 3. Scene document serialize/deserialize

Add a small scene document layer that can:

- parse scene_viewer scene files from disk
- emit the current scene_viewer state back to disk
- distinguish runtime scene data from editor-only metadata

The first version may stay narrowly scoped to `scene_viewer` instead of claiming to serialize arbitrary scene graphs.

### 4. Command bus scene I/O commands

Add builtin commands for:

- `scene load <path>`
- `scene save`
- `scene save <path>`

These commands should call the same scene document layer used by startup/shutdown flow so there is only one source of truth for scene persistence.

### 5. Verification coverage

Add automated checks for:

- initial fallback: `editor_cam` equals `game_cam` when metadata is absent
- preview toggle: active camera changes, but neither camera pose changes
- metadata restore: persisted editor pose comes back on reload
- scene save writes editor metadata and gameplay camera state
- scene load command rebuilds the same state through the command bus

## Risks And Tradeoffs

### Why not sync on every preview toggle?

Because that would blur the boundary between authored scene state and editor workspace state. It would also make command history and future save semantics harder to reason about.

### Why not store `editor_cam` as a normal scene node?

Because it pollutes runtime scene content with editor-only state, complicates scene authoring rules, and makes runtime consumers pay attention to a camera they should not care about.

### Why store editor camera state in the scene file?

Because the desired experience is scene-scoped continuity. The same scene should reopen to the same editor framing regardless of machine or workspace, which a per-user local session file would not provide.

### Why add explicit scene commands instead of only saving on exit?

Because command-driven editing is already a core project direction. Scene persistence should be reachable through the same command bus used by UI, automation, and future MCP control. Keeping load/save outside the command system would create a second control path with different behavior.

## Acceptance Criteria

- A scene with no editor metadata opens with `editor_cam` visually matching `game_cam`.
- `preview on` causes the main viewport to render from `game_cam`.
- `preview off` causes the main viewport to render from `editor_cam`.
- Preview toggling does not modify either camera's transform or projection settings.
- After moving the editor camera, saving, and reopening the scene, the editor viewport restores to the saved editor pose.
- Existing scenes without editor metadata continue to load through the fallback path without errors.
- `scene save` writes a scene document that includes runtime gameplay camera data plus editor-only editor camera metadata.
- `scene load <path>` restores that document through the same semantics as process startup.
