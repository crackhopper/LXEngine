# lxe-editor Specification

## Purpose

Define the default integration demo that doubles as a hand-on playground for the renderer. The `lxe_editor` demo lives under `src/demos/lxe_editor/`, is gated by a build option separate from tests, and drives its frame loop through `LX_core::gpu::EngineLoop` rather than any hand-rolled loop. The default scene contains a `DamagedHelmet.gltf` node, a ground quad, one `DirectionalLight`, and a controllable camera. The floating toolbar exposes the `Selection` editor mode separately from mutually exclusive camera controls (`Orbit` default, `FreeFly`) and preview/debug/preferences actions. UI panels are registered through `VulkanRenderer::setDrawUiCallback` and rely on `LX_infra::debug_ui` helpers where available. The glTF → current-material bridging is explicitly demo-local glue and does not get lowered into `src/infra/`. The demo is not registered with CTest; acceptance is a manual checklist captured in the demo's README.

## Requirements

### Requirement: Demo directory layout and build switch

A dedicated demo tree SHALL live under `src/demos/`. The first demo SHALL be `lxe_editor`, producing an executable target named `lxe_editor`. Top-level `CMakeLists.txt` SHALL expose `option(LX_BUILD_DEMOS "Build demo executables" ON)` and, when enabled, SHALL `add_subdirectory(src/demos)`. `src/demos/CMakeLists.txt` SHALL be the entry that includes individual demo subdirectories via `add_subdirectory(lxe_editor)`. Demo sources SHALL NOT live in `src/test/` and SHALL NOT be registered with CTest.

`src/demos/lxe_editor/` SHALL contain at minimum:

- `CMakeLists.txt`
- `main.cpp`
- `scene_builder.{hpp,cpp}` (glTF → Mesh / Material / SceneNode glue)
- `camera_rig.{hpp,cpp}` (Orbit / FreeFly controller + switching)
- `ui_overlay.{hpp,cpp}` (setDrawUiCallback target)
- `README.md`

#### Scenario: LX_BUILD_DEMOS=ON produces demo executable

- **WHEN** configuring with `LX_BUILD_DEMOS=ON` (default) and running `cmake --build build --target lxe_editor`
- **THEN** the build SHALL succeed and produce an executable at `build/src/demos/lxe_editor/lxe_editor`

#### Scenario: Demo is not registered with CTest

- **WHEN** running `ctest --test-dir build -N`
- **THEN** `lxe_editor` SHALL NOT appear in the enumerated test list

### Requirement: Demo runs on EngineLoop, not a hand-rolled loop

`src/demos/lxe_editor/main.cpp` SHALL drive the frame pump through `LX_core::gpu::EngineLoop::run()` rather than any bespoke `while (running) { uploadData(); draw(); }` loop. The startup sequence SHALL perform, in order:

1. Initialize the explicit runtime asset root and fail-fast (non-zero exit) on false
2. Construct `LX_infra::Window`
3. Construct `LX_core::backend::VulkanRenderer` via its factory
4. `renderer->initialize(window, "lxe_editor")`
5. Build the `Scene` (helmet + ground + default directional light + camera)
6. Construct `EngineLoop`
7. `loop.initialize(window, renderer)`
8. `loop.startScene(scene)`
9. `setUpdateHook(...)` with the per-frame demo callback
10. `renderer`'s `setDrawUiCallback(...)` with the UI overlay callback
11. `loop.run()`

Per-frame timing SHALL be read from the `Clock` passed into the update hook (or via `EngineLoop::getClock()`).

#### Scenario: No hand-rolled main loop

- **WHEN** grepping `src/demos/lxe_editor/main.cpp` for frame-pump constructs
- **THEN** neither `while (running)` over `renderer->uploadData()` / `renderer->draw()`, nor any standalone `renderer->draw()` call outside of `EngineLoop`, SHALL be found

#### Scenario: Startup fails fast when assets are missing

- **WHEN** runtime asset-root initialization fails
- **THEN** the demo SHALL exit with a non-zero status code and SHALL NOT construct the Vulkan renderer

### Requirement: Default scene contains helmet, ground, light, camera

The default scene SHALL contain, at minimum:

1. One `LX_core::SceneNode` rendering `DamagedHelmet.gltf`, loaded via the existing `infra::GLTFLoader`
2. One `LX_core::SceneNode` rendering a ground plane (static 20m × 20m XZ quad at `y = 0`)
3. One default `LX_core::DirectionalLight`
4. One controllable camera implemented as a `SceneNode` carrying `CameraComponent`

The demo SHALL NOT load `Sponza` in the first release; Sponza is a downstream extension target.

#### Scenario: Scene has helmet and ground

- **WHEN** the demo starts and the scene is built
- **THEN** both the helmet node and the ground node SHALL be present as renderables in the scene

#### Scenario: Directional light is editable

- **WHEN** the demo starts
- **THEN** a `DirectionalLight` SHALL exist in the scene and SHALL be reachable for UI editing

### Requirement: glTF → current material system is demo-local glue

`scene_builder.{hpp,cpp}` SHALL contain all glTF → current-material bridging logic. It SHALL NOT be lowered into `src/infra/`. Responsibilities:

- `buildMeshFromGltf(loader)` SHALL produce a `Mesh` using the existing `VertexPosNormalUvBone` layout, copying POSITION / NORMAL / TEXCOORD_0 / TANGENT (when present) from the `GLTFLoader` outputs; when `TANGENT` is absent, it SHALL use a controlled placeholder (e.g. `Vec4f{1, 0, 0, 1}`) and log a one-shot warning, and SHALL NOT generate tangents via MikkTSpace or any equivalent algorithm
- `makeHelmetMaterial(pbrMat, gltfDir)` SHALL start from `LX_infra::loadGenericMaterial("assets/materials/blinnphong_default.material")`, bridge `pbrMat.baseColorTexture` (when non-empty) into the material's albedo texture binding, set `enableAlbedo=1`, set `enableNormal=0` (DamagedHelmet.gltf declares no TANGENT), and finally call `syncGpuData()`
- Other glTF PBR textures (`metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveTexture`) SHALL NOT be auto-bridged in this change; they MAY be surfaced as read-only labels in the UI for future reference
- `buildGroundNode()` SHALL produce a ground `SceneNode` using the same vertex layout and the same `blinnphong_default.material` with `enableAlbedo=0` and a neutral `baseColor`

#### Scenario: scene_builder stays demo-local

- **WHEN** grepping the repository for `scene_builder.hpp`
- **THEN** includes SHALL only appear under `src/demos/lxe_editor/`, not from any path under `src/core/` or `src/infra/`

#### Scenario: Tangents are not generated when absent

- **WHEN** `buildMeshFromGltf` processes a `GLTFLoader` whose `getTangents()` returns empty
- **THEN** the mesh's tangent stream SHALL be filled with the chosen placeholder value AND no tangent-generation algorithm SHALL be invoked

### Requirement: Renderable path uses SceneNode

The demo SHALL express helmet, ground, and any additional renderables as `LX_core::SceneNode` instances attached to the `Scene`.

#### Scenario: Helmet and ground are SceneNode instances

- **WHEN** the demo builds its scene
- **THEN** both the helmet and ground renderables SHALL be instances of `LX_core::SceneNode`

### Requirement: Toolbar-driven editor and camera controls

The demo SHALL expose a floating toolbar window that is the primary editor interaction entry point. The toolbar SHALL provide four groups:

1. Editor mode controls. The current editor mode set contains `Selection` only.
2. Camera controls. `Orbit` and `FreeFly` are mutually exclusive camera control modes, and exactly one SHALL be selected.
3. Functional buttons: reset editor camera, preview, debug, and preferences.
4. Gizmo operation controls on the second toolbar row. `Translate`, `Rotate`, and `Scale` are mutually exclusive gizmo operations, and exactly one SHALL be selected when the editor overlay is visible.

Editor mode and camera control mode SHALL be orthogonal. Switching camera control SHALL NOT change editor mode. Selecting `Selection` SHALL NOT change camera control.

`Orbit` SHALL remain the default camera control mode. When switching between `Orbit` and `FreeFly`, the newly-activated controller SHALL be seeded from the current camera-node pose so that the view remains continuous across the switch.

At every frame's update hook:

- `Selection` interactions SHALL use the left mouse button and be suppressed by preview, UI mouse capture, and gizmo mouse capture.
- Camera controls SHALL use the right mouse button and be suppressed by preview, UI mouse capture, UI keyboard capture, and gizmo mouse capture.
- `Preview` SHALL disable selection, gizmo, and camera controls and render the gameplay camera instead.

Control mappings SHALL include:

- Selection: left-click pick, left-click empty space deselect, and gizmo hover/drag consumes left mouse while the gizmo is active
- Gizmo operation: toolbar second-row buttons select Translate / Rotate / Scale; `W`, `E`, and `R` select the same operations from the keyboard
- Orbit camera control: right-drag rotate, wheel zoom
- FreeFly: right-button hold rotate, `W/A/S/D` translate, `Space` up, `LShift` down, `LCtrl` accelerate

#### Scenario: View is continuous across camera control switch

- **WHEN** camera control is switched from Orbit to FreeFly while the camera is looking at a specific point
- **THEN** the immediate next frame's camera position and forward direction SHALL be identical to the pre-switch pose (up to numerical precision from yaw/pitch reconstruction)

#### Scenario: Preview suppresses edit interactions

- **WHEN** preview is enabled from the toolbar or the `F` hotkey
- **THEN** scene clicking, `Esc` deselect, and `Delete` remove SHALL NOT mutate editor state until preview is disabled

#### Scenario: Hidden toolbar config is recovered

- **WHEN** persisted local editor config marks the toolbar window as hidden
- **THEN** startup SHALL force the toolbar visible again so the mode switcher cannot be lost behind stale local state

### Requirement: UI overlay via VulkanRenderer::setDrawUiCallback

The demo SHALL register its UI drawing function through `LX_core::backend::VulkanRenderer::setDrawUiCallback(std::function<void()>)`. It SHALL NOT assume that `gpu::Renderer` exposes a UI callback API. The registered callback SHALL render, at minimum:

1. A **Toolbar** panel with `Selection` editor mode, `Orbit` / `FreeFly` camera controls, and functional buttons for reset editor camera, preview, debug, and preferences
2. A **Stats** panel showing frame count, delta time (ms), smoothed FPS, preview state, current editor mode, and current camera control mode
3. A **Scene Tree** panel and **Inspector** panel sharing editor state
4. A **Command Console** panel
5. A **Help** panel (demo-local) listing `F1`, `F`, toolbar-driven editor/camera controls, and the selection/preview interaction rules; toggled on `F1` rising edge
6. A **Preferences** panel exposing at least `preferences.uiFontScale`

#### Scenario: UI is injected through VulkanRenderer

- **WHEN** grepping `src/demos/lxe_editor/` for `setDrawUiCallback`
- **THEN** there SHALL be exactly one registration site inside `main.cpp` (or its direct helper) targeting the concrete `VulkanRenderer`

#### Scenario: Core editor panels are visible at startup

- **WHEN** running the demo with a display and the default Help visibility is ON
- **THEN** Toolbar / Stats / Scene Tree / Inspector / Command Console / Help SHALL be rendered at least once per frame

#### Scenario: Editing light color changes the frame

- **WHEN** the user drags the Directional Light `color` widget
- **THEN** `light.ubo->isDirty()` SHALL be set to `true` within that frame and the next rendered frame SHALL reflect the new light color

### Requirement: Demo README

`src/demos/lxe_editor/README.md` SHALL contain, at minimum, these sections:

1. Purpose of the demo
2. Upstream requirements it depends on (REQ-010 / 011 / 012 / 013 / 014 / 015 / 016 / 017 / 018 / 020)
3. How to build and run (including the `LD_LIBRARY_PATH` note for the vendored SDL3 shared library)
4. Controls reference (keyboard + mouse, for both Orbit and FreeFly)
5. Known limitations, at minimum:
   - Material bridging is a transitional demo glue, not full PBR
   - ImGui is a swapchain overlay, not a FrameGraph pass
   - SDL backend is the primary path; GLFW is not validated here
   - First release scene is DamagedHelmet; Sponza is not included

#### Scenario: README sections are present

- **WHEN** reading `src/demos/lxe_editor/README.md`
- **THEN** all five sections listed above SHALL be present and non-empty

### Requirement: Manual acceptance checklist

Because the demo is not automated, acceptance SHALL be verified manually. The minimum checklist is:

1. `lxe_editor` launches successfully
2. `DamagedHelmet` and ground are visible in the viewport
3. Orbit camera control allows right-drag rotate and wheel zoom without obvious artifacts
4. Switching the toolbar camera control to FreeFly keeps Selection as the editor mode; W/A/S/D/Space/LShift/LCtrl all move the camera while right mouse is held
5. ImGui panels are visible and interactive
6. Edits to Camera / Directional Light fields cause visible changes in the rendered frame
7. Closing the window exits cleanly without crashing

This checklist SHALL be reproduced (or referenced) in the README so that reviewers can execute it during review.

#### Scenario: Acceptance checklist is executable

- **WHEN** a reviewer follows the README's acceptance checklist after a successful build
- **THEN** all seven items SHALL pass on the SDL primary path
