# Proposal: Multi-camera / multi-light with RenderTarget-driven filtering

## Why

REQ-008 plumbed `FrameGraph → RenderQueue → RenderingItem` end-to-end but kept `Scene`'s single-camera / single-light assumption: `Scene::getSceneLevelResources()` is parameterless and every scene-level UBO is unconditionally merged into every `RenderingItem`. This is incompatible with any realistic rendering scenario — a main camera draws the swapchain, a shadow camera draws a shadow map, an overlay camera draws a separate attachment; a directional light participates in forward and deferred passes but the shadow-casting light set is different. Both dimensions (which target, which pass) need to filter scene-level resources independently.

This change makes `Scene` a multi-container, gives `Camera` a `RenderTarget` association, gives `Light` a pass mask, and adds two filtering axes to `getSceneLevelResources(pass, target)` so `RenderQueue::buildFromScene` can construct the correct per-pass resource list for every `FramePass`. `VulkanRenderer::initScene` will derive the real swapchain `RenderTarget` from the Vulkan device (no longer the placeholder `RenderTarget{}` REQ-008 left behind) and backfill it into every camera whose `m_target == nullopt`.

## What Changes

- **BREAKING** `LightBase` becomes an abstract interface with `getPassMask()`, `getUBO()`, and `supportsPass(pass)` virtuals. `DirectionalLight` implements it and holds an `m_passMask` defaulting to `Forward | Deferred`.
- Add `std::optional<RenderTarget> m_target` to `Camera`, plus `getTarget()` / `setTarget()` / `clearTarget()` / `matchesTarget(target)` accessors. `matchesTarget` returns `false` on `nullopt`; `VulkanRenderer::initScene` is responsible for backfilling nullopt cameras with the swapchain target before `m_frameGraph.buildFromScene` runs.
- Add `RenderTarget::operator==` (by-field comparison on `colorFormat` / `depthFormat` / `sampleCount`) in `src/core/gpu/render_target.hpp`.
- **BREAKING** `Scene` replaces the single `CameraSharedPtr camera` / `DirectionalLightSharedPtr directionalLight` fields with `std::vector<CameraSharedPtr> m_cameras` / `std::vector<LightBaseSharedPtr> m_lights`. Add `addCamera` / `addLight` / `getCameras` / `getLights`. The `m_renderables` container is unchanged.
- **BREAKING** `Scene::getSceneLevelResources()` (the parameterless REQ-008 version) is replaced by `getSceneLevelResources(StringID pass, const RenderTarget &target)`. Camera UBOs are filtered by `cam->matchesTarget(target)`; light UBOs are filtered by `light->supportsPass(pass)`. Order is still camera-first, light-second.
- **BREAKING** `RenderQueue::buildFromScene(scene, pass)` is replaced by `buildFromScene(scene, pass, target)`. `FrameGraph::buildFromScene(scene)` is updated to pass `pass.target` from each `FramePass` through.
- `VulkanRenderer::initScene` derives the swapchain `RenderTarget` from the Vulkan device via a new `makeSwapchainTarget()` helper (replacing the REQ-008 placeholder `defaultForwardTarget()`). A new `toImageFormat(VkFormat)` translation covering the VkFormats the swapchain actually uses is added to `src/backend/vulkan/details/vk_resource_manager.cpp` (next to the existing `toVkFormat`). `initScene` backfills nullopt cameras with the swapchain target **before** `m_frameGraph.buildFromScene(*scene)`.
- Migrate the five integration tests already moved onto `firstItemFromScene` in REQ-008 so they use `scene->addCamera(...)` / `scene->addLight(...)` and explicitly `setTarget(RenderTarget{})` on the added camera. Add a `makeDefaultCameraWithTarget` convenience helper in `scene_test_helpers.hpp`; extend `firstItemFromScene` to take an optional `const RenderTarget & = {}` parameter.
- Add three new scenarios to `test_frame_graph.cpp`:
  1. `testMultiCameraTargetFilter` — two cameras bound to distinct targets, assert `getSceneLevelResources(target)` returns only the matching camera's UBO.
  2. `testMultiLightPassMaskFilter` — three lights with different pass masks, assert per-pass resource lists honor the mask.
  3. `testNullOptCameraBeforeAndAfterFill` — camera with `nullopt` target matches no target; after `setTarget`, matches exactly that target. Validates the `VulkanRenderer::initScene` backfill contract.
- Add "Partial supersede by REQ-009" banner to `docs/requirements/finished/008-frame-graph-drives-rendering.md` scoped to R3 / R4 / R6 (the parameter-surface changes).

## Capabilities

### New Capabilities

_(none — only modifies existing capabilities)_

### Modified Capabilities

- `frame-graph`: `RenderQueue::buildFromScene` gains a `target` parameter; `Scene::getSceneLevelResources` gains `(pass, target)` parameters and filters camera by target / light by pass; `FrameGraph::buildFromScene` delegates with `pass.target`; `IRenderable::supportsPass` is unchanged; `Scene` is restated as a multi-camera / multi-light container.
- `renderer-backend-vulkan`: `VulkanRenderer::initScene` takes on responsibility for deriving the real swapchain `RenderTarget` and backfilling nullopt cameras. Requires the new `toImageFormat(VkFormat)` translation to exist in the backend.

### Untouched Capabilities (explicitly)

- `render-signature`: `passFlagFromStringID` and the `Pass_*` constants are unchanged.
- `pipeline-key` / `pipeline-build-info` / `pipeline-cache`: no changes.
- `string-interning`: no changes.

## Impact

**Affected source files**:
- `src/core/scene/light.hpp` — `LightBase` becomes abstract interface
- `src/core/scene/light.cpp` — new file (or add to existing) for `DirectionalLight::getUBO()` out-of-line if needed
- `src/core/scene/camera.hpp` / `camera.cpp` — `std::optional<RenderTarget> m_target` + accessors + `matchesTarget`
- `src/core/gpu/render_target.hpp` — `operator==`
- `src/core/scene/scene.hpp` / `scene.cpp` — multi-container + `getSceneLevelResources(pass, target)`
- `src/core/scene/render_queue.hpp` / `render_queue.cpp` — `buildFromScene(scene, pass, target)`
- `src/core/scene/frame_graph.cpp` — delegation with `pass.target`
- `src/backend/vulkan/vk_renderer.cpp` — `makeSwapchainTarget()`, nullopt backfill, `initScene` rewrite
- `src/backend/vulkan/details/vk_resource_manager.cpp` — `toImageFormat(VkFormat)` helper (covers `VK_FORMAT_B8G8R8A8_SRGB`, `VK_FORMAT_B8G8R8A8_UNORM`, `VK_FORMAT_D32_SFLOAT`, `VK_FORMAT_D24_UNORM_S8_UINT`)
- `src/backend/vulkan/details/vk_resource_manager.hpp` — declaration for `toImageFormat`
- `src/test/integration/scene_test_helpers.hpp` — `firstItemFromScene` target overload + `makeDefaultCameraWithTarget`
- `src/test/integration/test_frame_graph.cpp` — three new scenarios
- `src/test/integration/test_vulkan_command_buffer.cpp` / `test_vulkan_resource_manager.cpp` / `test_vulkan_pipeline.cpp` / `test_pipeline_cache.cpp` / `test_pipeline_build_info.cpp` — migrate setup to `addCamera/addLight + setTarget`

**Affected specs**:
- `openspec/specs/frame-graph/spec.md` — delta
- `openspec/specs/renderer-backend-vulkan/spec.md` — delta

**Requirement source**: `docs/requirements/009-multi-camera-multi-light.md` (REQ-009)

**Upstream dependency**: REQ-008 (archived as `openspec/changes/archive/2026-04-14-frame-graph-drives-rendering/`). This change cannot start without that baseline.

**Downstream unlock**: real shadow pass (shadow map as separate `FramePass.target`), deferred lighting (G-buffer target), overlay / UI pass (multiple cameras sharing swapchain target across passes), point/spot/area light types (new `LightBase` implementors), reflection probes (cubemap target).

**Compatibility**: Purely internal. All affected callers are in `src/` and `src/test/`; no external consumer depends on the deleted surfaces. The five migrated REQ-008 tests must be updated in lockstep or the build breaks loudly.
