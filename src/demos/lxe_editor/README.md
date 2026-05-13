# lxe_editor

The default playground demo. Starts with an empty scene, lets you manually load
built-in or local scene documents, renders them through the project's Vulkan
backend, and adds an ImGui editor MVP overlay with scene tree / inspector /
console plus a floating toolbar for Selection editor mode, Orbit / FreeFly
camera controls, and Preview.

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
cmake --build build --target lxe_editor
```

`LX_BUILD_DEMOS` is `ON` by default, so a plain `cmake --build build` also
produces the demo.

### Run

The vendored SDL3 ships as a shared library; point the loader at it:

```sh
export LD_LIBRARY_PATH=build/_deps/sdl3-build:$LD_LIBRARY_PATH
./build/src/demos/lxe_editor/lxe_editor
```

The demo now initializes an explicit runtime asset root and fails fast
with a non-zero exit code if the `assets/` tree cannot be found.

## Scene document behavior

- Startup begins with an empty scene. No sample scene is auto-loaded.
- Built-in scenes live under `assets/scenes/` and are listed as `asset`.
- User scenes and autosaved copies live under `data/scenes/` and are listed as
  `local`.
- Editor chrome persists locally under `data/lxe_editor/`:
  `editor_config.yaml` stores the native window position/size/maximized state,
  floating panel layout/collapsed state, and local editor preferences such as
  `uiFontScale`. `editor_data.yaml` stores editor data such as the last 50
  command-console history lines. Neither file stores the current scene path,
  selection, preview mode, or other scene-authored state. The toolbar layout is
  persisted in `editor_config.yaml`, but startup forces the toolbar visible
  again so the mode switcher cannot be lost behind a stale hidden-state entry.
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
| `mode [selection|status]` | Change or inspect the current editor mode |
| `cam control [orbit|freefly|status]` | Change or inspect the current camera control mode |
| `state summary` | Return a stable JSON snapshot with scene, dirty, mode, camera, preview, debug, permission, and active-camera info |
| `state selection` | Return selected paths, primary AABB, and the last successful pick hit point |
| `state cameras` | Return editor / gameplay camera poses and the active camera path |
| `state scene` | Return scene metadata such as document path, source kind, node count, camera count, and light count |
| `state toolbar` | Return the current toolbar mode, camera, preview, and debug flags |
| `pick <x> <y>` | Run a scene pick against the current main scene view rect from console / API |
| `quit` | Gracefully stop the editor loop so tests and tools can persist local editor state before exit |

## API server

`lxe_editor` now exposes a local API surface that reuses the same
command system as the in-app console.

### Launch arguments

```sh
./build/src/demos/lxe_editor/lxe_editor \
  --api-host 0.0.0.0 \
  --api-port 3768
```

Supported flags:

- `--api-enable`
- `--api-disable`
- `--api-host <host>`
- `--api-port <port>`

The API server is enabled by default. On startup the demo prints the bound
host, port, and token-file path.

### Token auth

- Token file: `data/lxe_editor/api_token.txt`
- HTTP: `Authorization: Bearer <token>`
- WebSocket: either the same header or `?token=<token>` on `/ws`

### HTTP endpoints

| Method | Path | Effect |
|--------|------|--------|
| `GET` | `/health` | Simple liveness check, no auth required |
| `POST` | `/api/command` | Execute a command-console line from JSON `{ "line": "..." }` |
| `GET` | `/api/state` | Return the full structured editor API snapshot |
| `GET` | `/api/state/summary` | Return scene / dirty / mode / camera / preview / debug summary |
| `GET` | `/api/state/selection` | Return selection and last-hit-point state |
| `GET` | `/api/state/cameras` | Return editor / gameplay camera state |
| `GET` | `/api/state/scene` | Return scene metadata |
| `GET` | `/api/state/toolbar` | Return toolbar mode + camera + preview + debug |
| `POST` | `/api/preview` | Toggle or force preview using `{ "action": "toggle" }` or `{ "enabled": true|false }` |
| `POST` | `/api/camera/reset-editor-to-game` | Copy `game_cam` pose onto `editor_cam` |
| `POST` | `/api/pick` | Run a pick using `{ "x": 400, "y": 300 }` |

### WebSocket

- Endpoint: `/ws`
- Inbound command frame:
  - `{"type":"command","line":"scene list"}`
- Outbound messages:
  - `{"type":"command.response","payload":...}`
  - event stream messages such as `command.executed`, `selection.changed`,
    `mode.changed`, `preview.changed`, `dirty.changed`, `scene.loaded`,
    and `scene.saved`

## MCP diagnostics

Codex-side MCP integration now goes through the standalone `lxe_manager`
server. `lxe_editor` publishes only its editor HTTP/WebSocket discovery in
`runtime_state.yaml`; it no longer exposes `POST /mcp` and no longer writes an
MCP URL into runtime state.

- Repo-local Codex config: `.codex/config.toml`
- Manager MCP endpoint: `http://127.0.0.1:3880/mcp` by default
- Bearer token env var: `LXE_MANAGER_MCP_BEARER_TOKEN`
- Editor runtime discovery file: `data/lxe_editor/runtime_state.yaml`
- Editor runtime discovery content: editor HTTP/WS host, port, token, and
  process metadata only

To point Codex at the local manager:

```sh
source scripts/lxe_manager/use_local_mcp.sh
codex
```

That helper rewrites `.codex/config.toml` to register `lxe_manager` at
`http://127.0.0.1:3880/mcp` and configures Codex to read the bearer token from
`LXE_MANAGER_MCP_BEARER_TOKEN`. Override the endpoint with
`LXE_MANAGER_URL=http://host:port/mcp` when needed.

PowerShell:

```powershell
scripts/lxe_manager/use_local_mcp.ps1
codex
```

To point Codex at a remote manager:

```sh
source scripts/lxe_manager/use_remote_mcp.sh https://manager.example.com/mcp <token>
codex
```

The remote helper writes the manager URL config and exports
`LXE_MANAGER_MCP_BEARER_TOKEN` only in the current shell, without committing
secrets to the repo.

Current MCP surface:

- Tools:
  - `lxe_editor_command`
  - `lxe_editor_get_summary`
  - `lxe_editor_get_selection`
  - `lxe_editor_get_cameras`
  - `lxe_editor_pick`
  - `lxe_editor_wait_for`
  - `lxe_editor_ensure_running`
- Resources:
  - `lxe-editor://summary`
  - `lxe-editor://selection`
  - `lxe-editor://cameras`
  - `lxe-editor://toolbar`
  - `lxe-editor://scene`

The MCP surface reuses the same editor state snapshots as the HTTP endpoints.
It is intended for Codex diagnostics; the official editor regression path
remains the HTTP API.

## API black-box tests

The editor now has a repo-local Python black-box suite under `tests/lxe_editor/`.

Typical run:

```sh
python3 -m compileall tests/lxe_editor
xvfb-run -a python3 -m unittest discover -s tests/lxe_editor -p 'test_*.py'
```

These tests launch a real `lxe_editor` process, drive it through the HTTP API,
and assert structured state. Low-level C++ tests remain for command, layout,
interaction, and transport internals.

## Controls

| Key / Mouse | Effect |
|-------------|--------|
| `F1` | Toggle the Help panel |
| `F` | Toggle preview between the editor and gameplay camera paths |
| Toolbar | Switch Selection editor mode and Orbit / FreeFly camera controls; trigger reset editor camera, preview, debug, and preferences |
| `Esc` | Deselect current node in Selection mode when preview is off |
| `Delete` | Remove the selected node when preview is off |

### Selection mode

- Left-click in the main scene view selects the hit node
- Left-click empty space clears the current selection
- The selected node draws a prominent debug AABB, and a successful pick keeps a
  debug hit-point marker visible until selection changes or clears
- Preview mode suppresses scene selection, `Esc`, and `Delete` so gameplay
  camera preview does not mutate editor state

### Orbit camera control (default)

- Right-drag — rotate around the target
- Wheel — zoom in / out

### FreeFly camera control

- Right-button held — look around
- `W` / `A` / `S` / `D` — translate forward / left / back / right
- `Space` — ascend
- `LShift` — descend
- `LCtrl` — hold to accelerate

Switching camera controls preserves the current view direction so the framing
stays continuous. Selection remains the editor mode while the camera control
changes.

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

1. `lxe_editor` launches and shows a window.
2. DamagedHelmet and the ground plane are visible in the main scene view.
3. Orbit camera control allows right-drag rotate and wheel zoom.
4. The floating toolbar switches camera control between Orbit / FreeFly without
   leaving Selection mode, and FreeFly uses
   `W`/`A`/`S`/`D`/`Space`/`LShift`/`LCtrl` as described above.
5. The Stats, Scene Tree, Inspector, Console, and Help panels are visible
   and interactive.
6. Gizmo handles can be clicked and dragged when visible.
7. Inspector edits and console commands mutate the same scene state through the
   command bus, and preview mode blocks selection / deselect / remove actions.
8. `scene list` shows both `asset` and `local` entries.
9. Loading `assets/scenes/lxe_editor.scene.yaml` restores Helmet, ground,
   light, and gameplay camera.
10. Saving a built-in scene in `user` mode creates a timestamped file under
   `data/scenes/` instead of overwriting the asset.
11. Closing a dirty scene shows a save/discard/cancel prompt and exits cleanly
    after `Save` or `Discard`.
12. Moving/resizing the main window, rearranging/collapsing editor panels, and
    changing `UI Font Scale` is restored on the next launch from
    `data/lxe_editor/editor_config.yaml`.
