## 1. Additive foundation (no breakage)

- [x] 1.1 Rewrite `src/core/scene/light.hpp` — `LightBase` becomes an abstract interface with `virtual ~LightBase() = default;`, pure virtuals `getPassMask()`, `getUBO()`, and a non-pure `virtual bool supportsPass(StringID pass) const` with default `(getPassMask() & passFlagFromStringID(pass)) != 0`. Add `LightBaseSharedPtr` alias. Include `core/scene/pass.hpp` + `core/gpu/render_resource.hpp`.
- [x] 1.2 `DirectionalLight` in `src/core/scene/light.hpp` (or split into `.cpp`) inherits from `LightBase`. Add `m_passMask` member (default `ResourcePassFlag::Forward | ResourcePassFlag::Deferred`), `setPassMask` / `getPassMask` methods. Implement `getUBO()` override returning `std::dynamic_pointer_cast<IRenderResource>(ubo)`.
- [x] 1.3 Add `std::optional<RenderTarget> m_target` member to `Camera` in `src/core/scene/camera.hpp` (include `core/gpu/render_target.hpp` + `<optional>`). Add `getTarget()` / `setTarget(RenderTarget)` / `clearTarget()` / `matchesTarget(const RenderTarget&)` — `matchesTarget` returns `m_target.has_value() && *m_target == target`.
- [x] 1.4 Add `bool operator==(const RenderTarget &) const` and `operator!=` to `src/core/gpu/render_target.hpp` (field-by-field compare `colorFormat` / `depthFormat` / `sampleCount`).
- [x] 1.5 Build the core target. Expect success — all changes are additive so far.

## 2. Scene multi-container API

- [x] 2.1 In `src/core/scene/scene.hpp`, add `std::vector<CameraSharedPtr> m_cameras;` and `std::vector<LightBaseSharedPtr> m_lights;` as private members. Add public `addCamera(CameraSharedPtr)` / `addLight(LightBaseSharedPtr)` / `getCameras()` / `getLights()` methods. Do NOT yet remove the legacy `CameraSharedPtr camera;` / `DirectionalLightSharedPtr directionalLight;` public fields — they stay until task 7 to keep intermediate steps compiling.
- [x] 2.2 Add the new `std::vector<IRenderResourcePtr> getSceneLevelResources(StringID pass, const RenderTarget &target) const;` **overload** declaration. Leave the REQ-008 parameterless `getSceneLevelResources()` in place for now.
- [x] 2.3 Implement the new overload in `src/core/scene/scene.cpp`: iterate `m_cameras` and push `cam->getUBO()` (as `IRenderResource`) when `cam->matchesTarget(target)`; then iterate `m_lights` and push `light->getUBO()` when `light->supportsPass(pass)`. Preserve camera-first / light-second ordering.
- [x] 2.4 Build the core target.

## 3. RenderQueue + FrameGraph delegation

- [x] 3.1 Add the three-argument overload `void buildFromScene(const Scene &scene, StringID pass, const RenderTarget &target);` to `src/core/scene/render_queue.hpp`.
- [x] 3.2 Implement the three-argument overload in `src/core/scene/render_queue.cpp`: mirror the REQ-008 body but call `scene.getSceneLevelResources(pass, target)` instead of the parameterless version.
- [x] 3.3 Update `FrameGraph::buildFromScene` in `src/core/scene/frame_graph.cpp` to call `pass.queue.buildFromScene(scene, pass.name, pass.target)`. This switches the framegraph path to the new entry while the legacy REQ-008 2-arg queue entry still exists for any remaining caller.
- [x] 3.4 Build the core target.

## 4. Backend: toImageFormat + makeSwapchainTarget

- [x] 4.1 Declare `LX_core::ImageFormat toImageFormat(VkFormat)` in `src/backend/vulkan/details/vk_resource_manager.hpp` (or a small new header if clearer — but adjacent to existing `toVkFormat`).
- [x] 4.2 Implement `toImageFormat` in `src/backend/vulkan/details/vk_resource_manager.cpp` with branches for `VK_FORMAT_B8G8R8A8_SRGB` / `VK_FORMAT_B8G8R8A8_UNORM` / `VK_FORMAT_R8G8B8A8_SRGB` / `VK_FORMAT_R8G8B8A8_UNORM` / `VK_FORMAT_D32_SFLOAT` / `VK_FORMAT_D24_UNORM_S8_UINT` / `VK_FORMAT_D32_SFLOAT_S8_UINT`. Default branch returns `ImageFormat::RGBA8` and logs a debug line via the existing `rendererDebugEnabled()` gate.
- [x] 4.3 In `src/backend/vulkan/vk_renderer.cpp`, replace `defaultForwardTarget()` with `makeSwapchainTarget()` on `Impl`. Body reads `device->getSurfaceFormat().format` and `device->getDepthFormat()` and converts via `toImageFormat`; sets `sampleCount = 1`.
- [x] 4.4 Build the backend target.

## 5. Backend: initScene backfill + FramePass target

- [x] 5.1 In `VulkanRendererImpl::initScene`, before `m_frameGraph.addPass(...)`, iterate `scene->getCameras()` and call `cam->setTarget(makeSwapchainTarget())` on any camera whose `getTarget().has_value() == false`. Reuse a local `RenderTarget swapchain = makeSwapchainTarget();` for consistency.
- [x] 5.2 Replace `m_frameGraph.addPass(FramePass{Pass_Forward, defaultForwardTarget(), {}})` with `m_frameGraph.addPass(FramePass{Pass_Forward, swapchain, {}})`.
- [x] 5.3 Verify ordering: backfill → addPass → `m_frameGraph.buildFromScene(*scene)`. The backfill must run before buildFromScene so `getSceneLevelResources(pass, target)` sees the populated target.
- [x] 5.4 Build the backend target.

## 6. Test migration

- [x] 6.1 Upgrade `firstItemFromScene` in `src/test/integration/scene_test_helpers.hpp` to take `const LX_core::RenderTarget &target = {}` as a third defaulted parameter. Internally call `q.buildFromScene(scene, pass, target)`.
- [x] 6.2 Add `inline LX_core::CameraSharedPtr makeDefaultCameraWithTarget()` helper to the same file — constructs a default Camera (matching the one Scene ctor used to create), calls `setTarget(RenderTarget{})`, returns it.
- [x] 6.3 Migrate `src/test/integration/test_vulkan_command_buffer.cpp`: replace `scene->camera = ...` / `scene->directionalLight = ...` with `scene->addCamera(...)` / `scene->addLight(...)`. For each added camera, explicitly call `cam->setTarget(RenderTarget{})`. Any loader that previously produced a camera without a target needs the explicit call.
- [x] 6.4 Migrate `src/test/integration/test_vulkan_resource_manager.cpp` the same way.
- [x] 6.5 Migrate `src/test/integration/test_vulkan_pipeline.cpp` the same way.
- [x] 6.6 Migrate `src/test/integration/test_pipeline_cache.cpp` the same way.
- [x] 6.7 Migrate `src/test/integration/test_pipeline_build_info.cpp` the same way (note: this file uses `buildItem()` helper — the helper's `Scene::create(sub)` needs a follow-up `scene->addCamera(makeDefaultCameraWithTarget())` + `scene->addLight(...)` if it relies on scene-level UBOs being present).
- [x] 6.8 Build the test suite. Any loader / scene-creation path that previously default-populated a camera needs to be re-examined — the camera is still created, but its target is `nullopt` until the test (or production backend) explicitly sets it.

## 7. Break: delete legacy surfaces

- [x] 7.1 Remove `CameraSharedPtr camera;` public field from `Scene` in `src/core/scene/scene.hpp`.
- [x] 7.2 Remove `DirectionalLightSharedPtr directionalLight;` public field from `Scene`.
- [x] 7.3 Remove REQ-008's parameterless `std::vector<IRenderResourcePtr> getSceneLevelResources() const;` declaration and implementation from `Scene`.
- [x] 7.4 Remove REQ-008's `void buildFromScene(const Scene &scene, StringID pass);` 2-argument overload declaration and implementation from `RenderQueue`.
- [x] 7.5 Update the `Scene::Scene(IRenderableSharedPtr)` constructor so it no longer creates a default `Camera` / `DirectionalLight` on the deleted fields. Production code and tests must explicitly `addCamera` / `addLight` going forward.
- [x] 7.6 Scan for survivors: `grep -rn "scene->camera\b\|scene\.camera\b\|scene->directionalLight\b\|scene\.directionalLight\b\|getSceneLevelResources()[^(]" src/` should return zero hits.
- [x] 7.7 Build the full project. Any remaining caller triggers a hard compile failure.

## 8. New test scenarios (test_frame_graph.cpp)

- [x] 8.1 Study `src/test/integration/test_frame_graph.cpp` — note the mock helpers (`makeRenderable`, `FakeShader`, `FakeMaterial`). Add a `makeMockCameraWithTarget(const RenderTarget &)` and `makeMockCameraNoTarget()` helper in the anonymous namespace. Add a `makeMockLight(ResourcePassFlag)` helper that returns a `LightBaseSharedPtr` pointing to a minimal subclass with a settable pass mask and a non-null UBO.
- [x] 8.2 Add `testMultiCameraTargetFilter`: create `targetA` / `targetB` (distinct sampleCount or depthFormat), two cameras each bound to one, build a scene with a renderable, add both cameras. Call `scene->getSceneLevelResources(Pass_Forward, targetA)`. Assert returned size == 1 and the element is camA's UBO. Repeat for targetB → camB.
- [x] 8.3 Add `testMultiLightPassMaskFilter`: three lights (Forward-only, Shadow-only, Forward|Shadow), one camera with `RenderTarget{}`, assert `getSceneLevelResources(Pass_Forward, RenderTarget{}).size() == 3` (1 camera + 2 lights — Forward-only + Forward|Shadow); assert `Pass_Shadow` variant returns 3 as well (1 camera + 2 lights — Shadow-only + Forward|Shadow).
- [x] 8.4 Add `testNullOptCameraBeforeAndAfterFill`: create a mock camera without calling setTarget. Assert `scene->getSceneLevelResources(Pass_Forward, RenderTarget{}).size() == 0`. Call `cam->setTarget(RenderTarget{})`. Assert `scene->getSceneLevelResources(Pass_Forward, RenderTarget{}).size() == 1`.
- [x] 8.5 Register all three new scenarios in `main()` alongside the existing REQ-008 ones.
- [x] 8.6 Build and run `test_frame_graph`. Confirm all scenarios pass.

## 9. Cross-module migration (scan + fix)

- [x] 9.1 Run `grep -rn "scene->camera\b\|scene\.camera\b\|scene->directionalLight\b\|scene\.directionalLight\b" src/` in the entire tree (including infra loaders, backend, tests). Every hit must be rewritten to go through `scene->getCameras()[i]` / `scene->getLights()[i]` or via a newly-added helper.
- [x] 9.2 Specifically audit `src/infra/loaders/blinnphong_material_loader.cpp` — if the loader needs a camera or light for UBO seeding, confirm it now uses the new container API.
- [x] 9.3 Audit `src/test/test_render_triangle.cpp` — if it uses the old single-camera accessor, migrate. Otherwise leave untouched; the backend does the backfill internally.
- [x] 9.4 Run `test_render_triangle` exit-code check once on a machine with a display (or record as manual verification step if no display is available).

## 10. REQ-009 doc archival

- [x] 10.1 Add "Partial supersede by REQ-009" banner to `docs/requirements/finished/008-frame-graph-drives-rendering.md` at the top of the 实施状态 section, scoped to R3 / R4 / R6 (parameterless `getSceneLevelResources`, two-arg `buildFromScene`, placeholder `defaultForwardTarget`). Banner text: "R3 / R4 / R6 的参数签名已被 REQ-009 替换为带 `(pass, target)` / `makeSwapchainTarget` 的版本; 其他 R 继续有效."
- [x] 10.2 Do NOT modify any other section of the finished REQ-008 doc.
- [x] 10.3 Do NOT archive `docs/requirements/009-multi-camera-multi-light.md` yet — that's the `/finish-req` step after code + spec-sync lands.
- [x] 10.4 Run `openspec validate multi-camera-multi-light --strict` and fix any issues.
- [x] 10.5 The actual `/opsx:archive` step is invoked separately by the user after all tasks are green.
