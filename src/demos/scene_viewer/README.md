# demo_scene_viewer

The default playground demo. Starts with an empty scene, lets you manually load
built-in or local scene documents, renders them through the project's Vulkan
backend, and adds an ImGui editor MVP overlay with scene tree / inspector /
console plus a floating toolbar for Selection / Orbit / FreeFly / Preview.

## Purpose

- Collapse the "does the engine actually run end-to-end?" question into a
  single executable
- Provide the default integration target for full scene load/save plus future
  scene features (Sponza, shadows, IBL, post-processing)
- Keep a human-friendly UI surface so selection / camera / light / transform
  tweaks are observable without editing source

This demo is **not** a tutorial and **not** a CI test.

## Upstream requirements

| REQ | Provides |
|-----|----------|
| REQ-010 | Asset layout + `cdToWhereAssetsExist` |
| REQ-011 | Real `GLTFLoader` (used on `DamagedHelmet`) |
| REQ-012 | `IInputState` interface |
| REQ-013 | `Sdl3InputState` and SDL event pump |
| REQ-014 | `Clock` and smoothed delta time |
| REQ-015 | `OrbitCameraController` |
| REQ-016 | `FreeFlyCameraController` |
| REQ-017 | ImGui overlay on `VulkanRenderer::setDrawUiCallback` |
| REQ-018 | `LX_infra::debug_ui` helpers (Stats/Help shell panels) |
| REQ-041-a | ImGui editor MVP panels / gizmo / preview flow |
| REQ-020 | `EngineLoop` driving the frame pump |

## Build & run

### Build

```sh
cmake --build build --target demo_scene_viewer
```

`LX_BUILD_DEMOS` is `ON` by default, so a plain `cmake --build build` also
produces the demo.

### Run

The vendored SDL3 ships as a shared library; point the loader at it:

```sh
export LD_LIBRARY_PATH=build/_deps/sdl3-build:$LD_LIBRARY_PATH
./build/src/demos/scene_viewer/demo_scene_viewer
```

The demo now initializes an explicit runtime asset root and fails fast
with a non-zero exit code if the `assets/` tree cannot be found.

## Scene document behavior

- Startup begins with an empty scene. No sample scene is auto-loaded.
- Built-in scenes live under `assets/scenes/` and are listed as `asset`.
- User scenes and autosaved copies live under `data/scenes/` and are listed as
  `local`.
- Editor chrome persists locally under `data/scene_viewer/`:
  `editor_config.yaml` stores the native window position/size/maximized state,
  floating panel layout/collapsed state, and local editor preferences such as
  `uiFontScale`. It does not store the current scene path, selection, preview
  mode, or other session state. The toolbar layout is persisted there too, but
  startup forces the toolbar visible again so the mode switcher cannot be lost
  behind a stale hidden-state entry.
- `game_cam` is the authored gameplay camera serialized in the scene document.
- `editor_cam` is editor-only state. It is restored from
  `editor.editorCamera` when present and otherwise falls back to the gameplay
  camera pose.
- Current scene documents persist the authored scene name, gameplay camera
  path, node list, transform hierarchy, built-in mesh/material references,
  directional lights, and editor-camera metadata.
- `scene load <path-or-id>` queues a new document and applies it on the next
  update tick rather than swapping the active scene immediately from the
  console call.
- `scene save` writes back to the current scene when allowed.
- When the current scene came from `asset` and the session is still `user`,
  `scene save` protects the built-in file and writes a timestamped `local`
  copy under `data/scenes/`.
- Closing a dirty scene prompts for `Save`, `Discard`, or `Cancel`. `Save`
  follows the same `scene save` rules.

## Console commands

| Command | Effect |
|---------|--------|
| `scene list` | List both built-in `asset` scenes and writable `local` scenes |
| `scene load <path-or-id>` | Queue a full scene reload for the next update tick |
| `scene save` | Save the current scene back to its current path when allowed, or redirect protected assets to a timestamped local copy |
| `scene save <path>` | Save the current scene to an explicit path |
| `admin on` | Enable admin mode so `scene save` may overwrite built-in assets |
| `admin off` | Return to normal user mode |
| `admin status` | Show the current permission level |

## Controls

| Key / Mouse | Effect |
|-------------|--------|
| `F1` | Toggle the Help panel |
| `F` | Toggle preview between the editor and gameplay camera paths |
| Toolbar | Switch Selection / Orbit / FreeFly editor modes |
| `Esc` | Deselect current node in Selection mode when preview is off |
| `Delete` | Remove the selected node when preview is off |

### Selection mode

- Left-click in the main scene view selects the hit node
- Left-click empty space clears the current selection
- Preview mode suppresses scene selection, `Esc`, and `Delete` so gameplay
  camera preview does not mutate editor state

### Orbit mode (default)

- Left-drag — rotate around the target
- Right-drag — pan the target
- Wheel — zoom in / out

### FreeFly mode

- Right-button held — look around
- `W` / `A` / `S` / `D` — translate forward / left / back / right
- `Space` — ascend
- `LShift` — descend
- `LCtrl` — hold to accelerate

Switching modes preserves the current view direction so the framing stays
continuous.

## Known limitations

- **Material bridging is transitional glue, not full PBR.** The demo uses the
  existing `blinnphong_0` shader. `baseColorTexture` is bridged into the
  `albedoMap` binding; `metallicRoughnessTexture`, `normalTexture`,
  `occlusionTexture`, and `emissiveTexture` are read from glTF but not wired
  to the shader. Full PBR is a downstream REQ.
- **DamagedHelmet.gltf in this repository does not declare TANGENT.** The
  demo uses a placeholder tangent value and keeps `enableNormal=0` so the
  placeholder is never sampled.
- **ImGui is a swapchain overlay, not a FrameGraph pass.** This matches
  REQ-017's design and is intentional.
- **SDL is the primary backend.** GLFW builds may compile but are not
  validated through this demo.
- **First release scene is DamagedHelmet + ground only.** Sponza and other
  scenes are downstream extension targets.

## Manual acceptance checklist

Reviewers: run through these after a successful build. The demo is not
registered with CTest.

1. `demo_scene_viewer` launches and shows a window.
2. DamagedHelmet and the ground plane are visible in the main scene view.
3. Orbit mode allows left-drag rotate, right-drag pan, and wheel zoom.
4. The floating toolbar switches between Selection / Orbit / FreeFly, and
   FreeFly uses `W`/`A`/`S`/`D`/`Space`/`LShift`/`LCtrl` as described above.
5. The Stats, Scene Tree, Inspector, Console, and Help panels are visible
   and interactive.
6. Inspector edits and console commands mutate the same scene state through the
   command bus, and preview mode blocks selection / deselect / remove actions.
7. `scene list` shows both `asset` and `local` entries.
8. Loading `assets/scenes/scene_viewer.scene.yaml` restores Helmet, ground,
   light, and gameplay camera.
9. Saving a built-in scene in `user` mode creates a timestamped file under
   `data/scenes/` instead of overwriting the asset.
10. Closing a dirty scene shows a save/discard/cancel prompt and exits cleanly
    after `Save` or `Discard`.
11. Moving/resizing the main window, rearranging/collapsing editor panels, and
    changing `UI Font Scale` is restored on the next launch from
    `data/scene_viewer/editor_config.yaml`.
