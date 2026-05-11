# Full Scene Asset Workspace Design

Date: 2026-05-11

## Context

`lxe_editor` currently supports a narrow YAML document that only persists:

- `scene.name`
- `gameCamera`
- `editor.editorCamera`

The runtime scene content itself is still rebuilt from hard-coded demo logic. That
is no longer sufficient. We now need a real scene asset workflow with these
properties:

- full scene import/export
- startup into an empty scene by default
- manual scene loading
- scene listing from both built-in assets and local user data
- safe separation between read-only built-in scenes and writable user scenes
- session-level user/admin permissions controlling whether built-in assets may be overwritten
- close-time save prompts that default to local user data, not repository assets

## Goals

- Define a full scene document that persists the complete authored scene, not only camera metadata.
- Distinguish built-in read-only `asset` scenes from writable `local` scenes.
- Start the editor with an empty scene instead of auto-loading a default demo scene.
- Add command-bus support for `scene list`, `scene load`, and full-scene `scene save`.
- Save dirty work to `data/` by default and keep that data outside version control.
- Add a minimal session permission model with `user` and `admin`.

## Non-Goals

- No account system, login flow, or persistent identity management.
- No binary resource embedding inside scene files.
- No generic reflection-driven serializer for every future component type in this change.
- No automatic restore of the last local scene on process startup.
- No change to the rule that external mesh/material/texture content lives in separate asset files.

## Scene Sources

Scene files use one schema but belong to two source classes.

### `asset`

- Stored under built-in scene roots such as `assets/scenes/`.
- Represents engine-provided sample or test scenes.
- Read-only by default in normal editor use.
- Loadable and listable by all users.

### `local`

- Stored under writable user roots under `data/`.
- Represents user workspace scenes, autosaves, and editable copies.
- Writable by default.
- Loadable and listable by all users.

The implementation must treat source class as a runtime property derived from the
resolved scene path, not from file schema differences.

## Permission Model

The editor must expose a small session-scoped permission model:

- `user`
- `admin`

Behavior:

- startup default is `user`
- `user` may load any scene and save any `local` scene
- `user` may not overwrite built-in `asset` scenes
- `admin` may overwrite built-in `asset` scenes

Required commands:

- `admin on`
- `admin off`
- `admin status`

This is a session flag only. It does not introduce authentication or persistence.

## Save Semantics

### Save target rules

- If the current scene source is `local`, `scene save` writes back to the same file.
- If the current scene source is `asset` and the session is `admin`, `scene save`
  writes back to that asset file.
- If the current scene source is `asset` and the session is `user`, `scene save`
  must not overwrite the asset file.
- If the current scene is empty or unnamed, `scene save` must create a new `local`
  scene file under `data/`.

### Asset-to-local fallback

When a `user` saves a loaded `asset` scene:

- the system creates a `local` copy under `data/`
- the filename includes a timestamp
- the current document path switches to that local file
- the save result must clearly state that the asset was protected and a local
  copy was written instead

Example shape:

- `data/scenes/test_scene.2026-05-11-153000.scene.yaml`

### Close-time save

If the editor closes with a dirty scene:

- the editor prompts whether to save
- choosing save follows the same rules as `scene save`
- the default target for empty or read-only-origin scenes is always `data/`

The close-time path must not silently overwrite built-in assets in `user` mode.

## Startup Semantics

- The editor starts with an empty scene.
- No scene is loaded automatically on startup.
- The empty scene is a valid scene runtime state and must still provide editor and
  preview cameras, command routing, and overlay operation.
- Users load scenes explicitly through commands or later UI on top of the same runtime APIs.

## Scene List Semantics

The command bus must expose:

- `scene list`

`scene list` must:

- enumerate both built-in `asset` scenes and writable `local` scenes
- mark each entry with its source kind
- show a stable display name and resolved path
- return a structured payload that later UI or tooling can consume

`scene load` must accept both kinds.

## Full Scene Document Contract

The new scene document must persist complete scene state using resource references.

Minimum required content:

- scene name
- scene node hierarchy
- node names / paths
- local transforms
- camera components
- light components
- mesh component resource references
- material component resource references
- texture references where owned by serialized component/material state
- editor-only metadata, including editor camera state

The file must not embed the binary contents of meshes, materials, or textures.
It stores references only.

## First-Version Supported Scene Content

To keep the implementation bounded, the first serializer/deserializer must fully
support the engine types already exercised by `lxe_editor`:

- renderable `SceneNode`
- parent/child hierarchy
- `MeshComponent`
- `MaterialComponent`
- `CameraComponent`
- scene directional light setup
- editor-only camera metadata

If a scene file contains unknown or unsupported serialized component kinds, load
must fail in-band with a clear error that names the file and the affected node.

## Empty Scene Behavior

The empty startup scene must:

- create a valid `Scene`
- create editor and gameplay cameras using the existing preview semantics
- default the gameplay camera to a sensible authored pose
- default the editor camera from gameplay camera when no editor metadata exists
- contain no helmet, ground, or built-in sample content unless a scene file is loaded

## Command Surface

The command bus must support at least:

- `scene list`
- `scene load <name-or-path>`
- `scene save`
- `scene save <path>`
- `admin on`
- `admin off`
- `admin status`

Recommended command behavior:

- `scene list` shows both `asset` and `local` entries
- `scene load <name-or-path>` resolves either a catalog name or explicit file path
- `scene save` uses current path rules
- `scene save <path>` writes explicitly to that path, then treats it as current
- `admin status` reports the current session role

Failures must stay in-band through `CommandResult`.

## Runtime Integration

The runtime should be organized around three responsibilities:

### Scene document I/O

- serialize and deserialize the full scene file
- serialize and deserialize editor-only metadata

### Scene catalog

- scan built-in scene roots
- scan local data roots
- classify entries as `asset` or `local`
- resolve `scene load` names against that catalog

### Editor session state

- current scene path
- current source kind
- dirty flag
- current permission level
- default local save destination generation

These responsibilities should remain separate so scene listing, scene saving,
and editor shutdown all use the same rules.

## Preview Camera Semantics

The previously designed preview behavior remains unchanged:

- `game_cam` is the authored runtime camera
- `editor_cam` is editor-only
- preview toggles active camera only
- no transform sync occurs during preview toggle

This change broadens what the scene file can hold; it does not redefine camera ownership.

## Verification Requirements

Automated coverage must include:

- empty startup scene bootstraps correctly
- full scene save/load round-trips supported node hierarchy and components
- `scene list` returns both `asset` and `local` entries with type markers
- `scene save` on an `asset` scene in `user` mode creates a timestamped `local` copy
- `scene save` on an `asset` scene in `admin` mode overwrites the asset
- `scene save` on a `local` scene preserves local-in-place behavior
- preview camera semantics still hold after full scene serialization is introduced
- close-time save routing follows the same asset/local/admin rules

## Risks And Tradeoffs

- The current scene system was not originally built around a generic serializer,
  so the first full-scene implementation should stay explicit rather than trying
  to invent a reflection system in one step.
- Built-in asset protection adds path-classification logic that must be kept
  centralized. Ad hoc path checks in command handlers would be brittle.
- Starting with an empty scene means `lxe_editor` can no longer assume sample
  content exists at boot. Tests and overlays must tolerate an intentionally blank scene.
- Supporting both `asset` and `local` in one catalog means command UX gets
  better, but the output must clearly label source kind to avoid accidental edits.
