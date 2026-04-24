# demo-scene-viewer Specification

## Purpose

Define the default integration demo that doubles as a hand-on playground for the renderer. The `scene_viewer` demo lives under `src/demos/scene_viewer/`, is gated by a build option separate from tests, and drives its frame loop through `LX_core::gpu::EngineLoop` rather than any hand-rolled loop. The default scene contains a `DamagedHelmet.gltf` node, a ground quad, one `DirectionalLight`, and a controllable camera. Two camera controllers (Orbit default, FreeFly) are available with `F2` rising-edge switching. UI panels are registered through `VulkanRenderer::setDrawUiCallback` and rely on `LX_infra::debug_ui` helpers where available. The glTF → current-material bridging is explicitly demo-local glue and does not get lowered into `src/infra/`. The demo is not registered with CTest; acceptance is a manual checklist captured in the demo's README.

## Requirements

### Requirement: Demo directory layout and build switch

A dedicated demo tree SHALL live under `src/demos/`. The first demo SHALL be `scene_viewer`, producing an executable target named `demo_scene_viewer`. Top-level `CMakeLists.txt` SHALL expose `option(LX_BUILD_DEMOS "Build demo executables" ON)` and, when enabled, SHALL `add_subdirectory(src/demos)`. `src/demos/CMakeLists.txt` SHALL be the entry that includes individual demo subdirectories via `add_subdirectory(scene_viewer)`. Demo sources SHALL NOT live in `src/test/` and SHALL NOT be registered with CTest.

`src/demos/scene_viewer/` SHALL contain at minimum:

- `CMakeLists.txt`
- `main.cpp`
- `scene_builder.{hpp,cpp}` (glTF → Mesh / Material / SceneNode glue)
- `camera_rig.{hpp,cpp}` (Orbit / FreeFly controller + switching)
- `ui_overlay.{hpp,cpp}` (setDrawUiCallback target)
- `README.md`

#### Scenario: LX_BUILD_DEMOS=ON produces demo executable

- **WHEN** configuring with `LX_BUILD_DEMOS=ON` (default) and running `cmake --build build --target demo_scene_viewer`
- **THEN** the build SHALL succeed and produce an executable at `build/src/demos/scene_viewer/demo_scene_viewer`

#### Scenario: Demo is not registered with CTest

- **WHEN** running `ctest --test-dir build -N`
- **THEN** `demo_scene_viewer` SHALL NOT appear in the enumerated test list

### Requirement: Demo runs on EngineLoop, not a hand-rolled loop

`src/demos/scene_viewer/main.cpp` SHALL drive the frame pump through `LX_core::gpu::EngineLoop::run()` rather than any bespoke `while (running) { uploadData(); draw(); }` loop. The startup sequence SHALL perform, in order:

1. Initialize the explicit runtime asset root and fail-fast (non-zero exit) on false
2. Construct `LX_infra::Window`
3. Construct `LX_core::backend::VulkanRenderer` via its factory
4. `renderer->initialize(window, "demo_scene_viewer")`
5. Build the `Scene` (helmet + ground + default directional light + camera)
6. Construct `EngineLoop`
7. `loop.initialize(window, renderer)`
8. `loop.startScene(scene)`
9. `setUpdateHook(...)` with the per-frame demo callback
10. `renderer`'s `setDrawUiCallback(...)` with the UI overlay callback
11. `loop.run()`

Per-frame timing SHALL be read from the `Clock` passed into the update hook (or via `EngineLoop::getClock()`).

#### Scenario: No hand-rolled main loop

- **WHEN** grepping `src/demos/scene_viewer/main.cpp` for frame-pump constructs
- **THEN** neither `while (running)` over `renderer->uploadData()` / `renderer->draw()`, nor any standalone `renderer->draw()` call outside of `EngineLoop`, SHALL be found

#### Scenario: Startup fails fast when assets are missing

- **WHEN** runtime asset-root initialization fails
- **THEN** the demo SHALL exit with a non-zero status code and SHALL NOT construct the Vulkan renderer

### Requirement: Default scene contains helmet, ground, light, camera

The default scene SHALL contain, at minimum:

1. One `LX_core::SceneNode` rendering `DamagedHelmet.gltf`, loaded via the existing `infra::GLTFLoader`
2. One `LX_core::SceneNode` rendering a ground plane (static 20m × 20m XZ quad at `y = 0`)
3. One default `LX_core::DirectionalLight`
4. One controllable camera (the default camera produced by `Scene` is sufficient)

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
- `makeHelmetMaterial(pbrMat, gltfDir)` SHALL start from `LX_infra::loadGenericMaterial("materials/blinnphong_default.material")`, bridge `pbrMat.baseColorTexture` (when non-empty) into the material's albedo texture binding, set `enableAlbedo=1`, set `enableNormal=0` (DamagedHelmet.gltf declares no TANGENT), and finally call `syncGpuData()`
- Other glTF PBR textures (`metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveTexture`) SHALL NOT be auto-bridged in this change; they MAY be surfaced as read-only labels in the UI for future reference
- `buildGroundNode()` SHALL produce a ground `SceneNode` using the same vertex layout and the same `blinnphong_default.material` with `enableAlbedo=0` and a neutral `baseColor`

#### Scenario: scene_builder stays demo-local

- **WHEN** grepping the repository for `scene_builder.hpp`
- **THEN** includes SHALL only appear under `src/demos/scene_viewer/`, not from any path under `src/core/` or `src/infra/`

#### Scenario: Tangents are not generated when absent

- **WHEN** `buildMeshFromGltf` processes a `GLTFLoader` whose `getTangents()` returns empty
- **THEN** the mesh's tangent stream SHALL be filled with the chosen placeholder value AND no tangent-generation algorithm SHALL be invoked

### Requirement: Renderable path uses SceneNode

The demo SHALL express helmet, ground, and any additional renderables as `LX_core::SceneNode` instances attached to the `Scene`.

#### Scenario: Helmet and ground are SceneNode instances

- **WHEN** the demo builds its scene
- **THEN** both the helmet and ground renderables SHALL be instances of `LX_core::SceneNode`

### Requirement: Camera controllers with F2 edge-triggered switching

The demo SHALL register both `OrbitCameraController` and `FreeFlyCameraController`. Orbit SHALL be the default mode. Pressing the `F2` key SHALL switch between modes on a rising-edge transition; holding `F2` SHALL NOT cause repeated toggles. At every frame's update hook, the active controller SHALL be updated with the input state and the frame's delta time, followed by a call to `camera.updateMatrices()`. Edge detection MAY be implemented locally inside the demo using a `bool m_prevF2Down` comparison, since `Sdl3InputState` only exposes level state.

When switching modes, the newly-activated controller SHALL be seeded from the current camera state (position, target / yaw-pitch, distance) so that the view remains continuous across the switch.

Control mappings SHALL include:

- Orbit: left-drag rotate, right-drag pan target, wheel zoom
- FreeFly: right-button hold rotate, `W/A/S/D` translate, `Space` up, `LShift` down, `LCtrl` accelerate

#### Scenario: F2 rising edge toggles mode exactly once

- **WHEN** the user presses and holds `F2` for many frames
- **THEN** the active mode SHALL toggle exactly once (on the initial press) and SHALL NOT toggle again until the key is released and pressed again

#### Scenario: View is continuous across mode switch

- **WHEN** mode is switched from Orbit to FreeFly while the camera is looking at a specific point
- **THEN** the immediate next frame's camera position and forward direction SHALL be identical to the pre-switch pose (up to numerical precision from yaw/pitch reconstruction)

### Requirement: UI overlay via VulkanRenderer::setDrawUiCallback

The demo SHALL register its UI drawing function through `LX_core::backend::VulkanRenderer::setDrawUiCallback(std::function<void()>)`. It SHALL NOT assume that `gpu::Renderer` exposes a UI callback API. The registered callback SHALL render, at minimum:

1. A **Render Stats** panel showing frame count, delta time (ms), and smoothed FPS — using `LX_infra::debug_ui::renderStatsPanel(clock)` when available
2. A **Camera** panel editing `position`, `target`, `up`, `fovY`, `aspect`, `nearPlane`, `farPlane` — using `LX_infra::debug_ui::cameraPanel(...)` when available
3. A **Directional Light** panel editing `ubo->param.dir` and `ubo->param.color` — using `LX_infra::debug_ui::directionalLightPanel(...)` when available; the helper SHALL be responsible for calling `setDirty()` on user edits
4. A **Help** panel (demo-local) listing `F1`, `F2`, Orbit controls, FreeFly controls; toggled on `F1` rising edge

#### Scenario: UI is injected through VulkanRenderer

- **WHEN** grepping `src/demos/scene_viewer/` for `setDrawUiCallback`
- **THEN** there SHALL be exactly one registration site inside `main.cpp` (or its direct helper) targeting the concrete `VulkanRenderer`

#### Scenario: Four panels are visible at startup

- **WHEN** running the demo with a display and the default Help visibility is ON
- **THEN** all four panels (Stats / Camera / Directional Light / Help) SHALL be rendered at least once per frame

#### Scenario: Editing light color changes the frame

- **WHEN** the user drags the Directional Light `color` widget
- **THEN** `light.ubo->isDirty()` SHALL be set to `true` within that frame and the next rendered frame SHALL reflect the new light color

### Requirement: Demo README

`src/demos/scene_viewer/README.md` SHALL contain, at minimum, these sections:

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

- **WHEN** reading `src/demos/scene_viewer/README.md`
- **THEN** all five sections listed above SHALL be present and non-empty

### Requirement: Manual acceptance checklist

Because the demo is not automated, acceptance SHALL be verified manually. The minimum checklist is:

1. `demo_scene_viewer` launches successfully
2. `DamagedHelmet` and ground are visible in the viewport
3. Orbit mode allows rotate / pan / zoom without obvious artifacts
4. Pressing `F2` switches to FreeFly; W/A/S/D/Space/LShift/LCtrl all move the camera
5. ImGui panels are visible and interactive
6. Edits to Camera / Directional Light fields cause visible changes in the rendered frame
7. Closing the window exits cleanly without crashing

This checklist SHALL be reproduced (or referenced) in the README so that reviewers can execute it during review.

#### Scenario: Acceptance checklist is executable

- **WHEN** a reviewer follows the README's acceptance checklist after a successful build
- **THEN** all seven items SHALL pass on the SDL primary path
