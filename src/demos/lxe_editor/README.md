# lxe_editor

The default playground demo. Starts with an empty scene, lets you initialize or
open a project, renders the active project scene through the project's Vulkan
backend, and adds an ImGui editor MVP overlay with scene tree / inspector /
console plus a floating toolbar for Selection editor mode, Orbit / FreeFly
camera controls, and Preview.

## Purpose

- Collapse the "does the engine actually run end-to-end?" question into a
  single executable
- Provide the default integration target for full project scene editing plus future
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

### Display selection

List available displays:

```sh
./build/src/demos/lxe_editor/lxe_editor --display-list
```

Launch on a display by index:

```sh
./build/src/demos/lxe_editor/lxe_editor --display 0
```

Launch on a stable display profile id:

```sh
./build/src/demos/lxe_editor/lxe_editor --display "sdl:1:DELL U2720Q:3840x2160:1.50"
```

`data/lxe_editor/editor_config.yaml` stores display preferences under
`displayDefault` plus per-display overrides. `activeDisplay` records the
preferred display for the next launch and may be edited directly when the
editor is closed. A CLI `--display` value overrides the config for that launch;
when the editor saves configuration, that launched display becomes the saved
`activeDisplay`.

## Scene document behavior

- Startup opens the last project recorded in `data/lxe_editor/editor_data.yaml`
  when possible. If there is no last project, the editor begins with an empty
  unsaved scene.
- Project templates live under `assets/project_templates/` and are read-only.
  `project init <template-id> [project-name]` copies a template into
  `data/projects/` and opens the project's active scene.
- A project can contain multiple scenes. Scene ids and scene paths are resolved
  only inside the current project.
- Editor chrome persists locally under `data/lxe_editor/`:
  `editor_config.yaml` stores the native window position/size/maximized state,
  floating panel layout/collapsed state, and local editor preferences such as
  `uiFontScale`. `editor_data.yaml` stores editor data such as the last 50
  command-console history lines and the last opened project. The toolbar layout
  is persisted in `editor_config.yaml`, but startup forces the toolbar visible
  again so the mode switcher cannot be lost behind a stale hidden-state entry.
- `game_cam` is the authored gameplay camera serialized in the scene document.
- `editor_cam` is editor-only state. It is restored from
  `editor.editorCamera` when present and otherwise falls back to the gameplay
  camera pose.
- Current scene documents persist the authored scene name, gameplay camera
  path, node list, transform hierarchy, built-in mesh/material references,
  directional lights, and editor-camera metadata.
- `scene open <scene-id-or-path>` queues a project scene and applies it on the
  next update tick rather than swapping the active runtime scene immediately
  from the console call.
- `scene save` writes the currently loaded runtime scene back to the active
  project scene and saves the project metadata.
- Closing a dirty scene prompts for `Save`, `Discard`, or `Cancel`. `Save`
  follows the same `scene save` rules.

## Console commands

| Command | Effect |
|---------|--------|
| `project templates [list]` | List available read-only project templates |
| `project list` | List initialized projects under `data/projects/` |
| `project init <template-id> [project-name]` | Create a writable project from a template and queue its active scene |
| `project open <project-id-or-path>` | Open an existing project and queue its active scene |
| `project save` | Save the active project scene and `project.yaml` |
| `project status` | Return the current project summary |
| `project close` | Close the project, cancel pending scene opens, and return to an empty scene |
| `scene list` | List scenes registered in the current project |
| `scene open <scene-id-or-path>` | Queue a project-scoped scene open for the next update tick |
| `scene save` | Save the loaded runtime scene to the active project scene |
| `scene new <scene-id>` | Create a new project scene and queue it |
| `scene duplicate <source-id> <new-id>` | Copy a project scene and queue the duplicate |
| `scene remove <scene-id>` | Remove a non-active, non-last project scene |
| `mode [selection|status]` | Change or inspect the current editor mode |
| `cam control [orbit|freefly|status]` | Change or inspect the current camera control mode |
| `cam look-at <eye-x> <eye-y> <eye-z> <target-x> <target-y> <target-z>` | Place the active camera at a specific eye/target pose; with preview off this drives `editor_cam` |
| `state summary` | Return a stable JSON snapshot with scene, project, dirty, mode, camera, preview, debug, and active-camera info |
| `state selection` | Return selected paths, primary AABB, and the last successful pick hit point |
| `state cameras` | Return editor / gameplay camera poses and the active camera path |
| `state scene` | Return current runtime scene metadata such as scene name, dirty flag, node count, camera count, and light count |
| `state toolbar` | Return the current toolbar mode, camera, preview, and debug flags |
| `pick <x> <y>` | Run a scene pick against the current main scene view rect from console / API |
| `recording status` | Return recorder enabled/active/detail/save state |
| `recording enable` / `recording disable [force]` | Turn recording hooks on or off |
| `recording start [basic|diagnostic|trace]` | Start one recorder session |
| `recording stop [save|discard]` | Stop the current recorder session and optionally save it |
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
| `GET` | `/api/state/summary` | Return scene / project / dirty / mode / camera / preview / debug summary |
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
  - event stream messages such as `command.executed`, `project.opened`,
    `active_scene.changed`, `selection.changed`, `mode.changed`,
    `preview.changed`, `dirty.changed`, and `scene.saved`

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

Start the local manager in one terminal:

```sh
scripts/lxe_manager/start_mcp.sh
```

Then point Codex at it from another terminal:

```sh
export LXE_MANAGER_MCP_BEARER_TOKEN=<token-from-manager-output>
cd /home/lixiang/proj/LXEngine
source scripts/lxe_manager/use_local_mcp.sh
codex
```

That helper rewrites `.codex/config.toml` to register `lxe_manager` at
`http://127.0.0.1:3880/mcp` and configures Codex to read the bearer token from
`LXE_MANAGER_MCP_BEARER_TOKEN`. Override the endpoint with
`LXE_MANAGER_URL=http://host:port/mcp` when needed.

PowerShell:

```powershell
$Env:LXE_MANAGER_MCP_BEARER_TOKEN = "<token-from-manager-output>"
scripts/lxe_manager/use_local_mcp.ps1
codex
```

To point Codex at a remote manager:

```sh
scripts/lxe_manager/start_mcp.sh 0.0.0.0 3880
```

PowerShell:

```powershell
scripts/lxe_manager/start_mcp.ps1 0.0.0.0 3880
```

If a fixed bearer token is needed at startup, pass it as the third positional
argument:

```powershell
scripts/lxe_manager/start_mcp.ps1 0.0.0.0 3880 <token>
```

On the remote Codex client:

```sh
export LXE_MANAGER_MCP_BEARER_TOKEN=<token>
source scripts/lxe_manager/use_remote_mcp.sh http://manager.example.com:3880/mcp
codex
```

The remote helper writes the manager URL config and exports
`LXE_MANAGER_MCP_BEARER_TOKEN` only in the current shell, without committing
secrets to the repo.

`lxe_manager` prints `bearerToken` during startup. Copy that value into
`LXE_MANAGER_MCP_BEARER_TOKEN` on the Codex client.
See `notes/tools/lxe-manager-mcp.md` for the full service guide.

Current MCP surface:

- Tools:
  - `lxe_editor_command`
  - `lxe_editor_get_summary`
  - `lxe_editor_get_selection`
  - `lxe_editor_get_cameras`
  - `lxe_editor_pick`
  - `lxe_editor_wait_for`
  - `lxe_editor_ensure_running`
  - `recording_status`
  - `recording_enable`
  - `recording_start`
  - `recording_stop`
  - `recording_list`
  - `recording_read`
  - `recording_replay`
  - `recording_probe`
  - `display_list`
  - `display_active`
  - `display_config_get`
  - `display_config_set`
  - `display_select`
  - `ops.manager_restart`
- Resources:
  - `lxe-editor://summary`
  - `lxe-editor://selection`
  - `lxe-editor://cameras`
  - `lxe-editor://toolbar`
  - `lxe-editor://scene`

The MCP surface reuses the same editor state snapshots as the HTTP endpoints.
It is intended for Codex diagnostics; the official editor regression path
remains the HTTP API.

Recording is disabled by default. It can be controlled either through manager
MCP `recording_*` tools or through command-console commands such as
`recording enable`, `recording start basic`, and `recording stop save`.
Completed recordings are saved under `data/lxe_editor/recordings/` and can be
replayed through `recording_replay` for debug-first reproduction.

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
| Toolbar | Switch Selection editor mode and Orbit / FreeFly camera controls; trigger reset editor camera, preview, debug, recording, and preferences |
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
- Right + `M` — move the orbit target to the scene pick hit under the cursor
- Right + `W` / `A` / `S` / `D` — pan the orbit target in the camera plane
- The orbit target marker is editor-only debug geometry and is not selectable

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
3. Orbit camera control allows right-drag rotate, wheel zoom, `Right+M`
   retargeting, and `Right+W`/`A`/`S`/`D` target panning.
4. The floating toolbar switches camera control between Orbit / FreeFly without
   leaving Selection mode, and FreeFly uses
   `W`/`A`/`S`/`D`/`Space`/`LShift`/`LCtrl` as described above.
5. The Stats, Scene Tree, Inspector, Console, and Help panels are visible
   and interactive.
6. Gizmo handles can be clicked and dragged when visible.
7. Inspector edits and console commands mutate the same scene state through the
   command bus, and preview mode blocks selection / deselect / remove actions.
8. `project templates` lists the built-in `empty` template.
9. `project init empty smoke-project` creates `data/projects/smoke-project/`
   and queues its active scene.
10. `scene list` shows scenes registered in the open project.
11. `scene save` writes the loaded runtime scene to the active project scene.
12. Closing a dirty scene shows a save/discard/cancel prompt and exits cleanly
    after `Save` or `Discard`.
13. Moving/resizing the main window, rearranging/collapsing editor panels, and
    changing `UI Font Scale` is restored on the next launch from
    `data/lxe_editor/editor_config.yaml`.
