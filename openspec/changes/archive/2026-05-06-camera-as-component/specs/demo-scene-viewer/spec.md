## MODIFIED Requirements

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

### Requirement: Camera controllers with F2 edge-triggered switching
The demo SHALL register both `OrbitCameraController` and `FreeFlyCameraController`. Orbit SHALL be the default mode. Pressing the `F2` key SHALL switch between modes on a rising-edge transition; holding `F2` SHALL NOT cause repeated toggles. At every frame's update hook, the active controller SHALL be updated with the input state and the frame's delta time, followed by explicit refresh of the active `CameraComponent`'s GPU-facing matrices/resources from the current viewport state.

When switching modes, the newly-activated controller SHALL be seeded from the current camera-node pose so that the view remains continuous across the switch.

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
2. A **Camera** panel editing the active camera node’s transform and camera-component properties needed for projection / target binding / culling configuration
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
