## 1. Core helpers (additive, no breakage)

- [x] 1.1 Add `LX_core::ResourcePassFlag passFlagFromStringID(StringID pass)` declaration to `src/core/scene/pass.hpp`
- [x] 1.2 Create new `src/core/scene/pass.cpp` implementing `passFlagFromStringID` with branches for `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow`, defaulting to `ResourcePassFlag{0}` for unknown IDs
- [x] 1.3 Add `pass.cpp` to `src/core/CMakeLists.txt` (or whichever target includes scene sources)
- [x] 1.4 Add `virtual bool supportsPass(StringID pass) const` to `IRenderable` in `src/core/scene/object.hpp` with default implementation `(getPassMask() & passFlagFromStringID(pass)) != 0`. Verify `object.hpp` includes `core/scene/pass.hpp` (directly or transitively)
- [x] 1.5 Add unit-level smoke assertions: `passFlagFromStringID(Pass_Forward) == ResourcePassFlag::Forward` inside an existing scene unit test, or alongside R8 in `test_frame_graph.cpp`

## 2. Scene data-container API

- [x] 2.1 Add `std::vector<IRenderResourcePtr> getSceneLevelResources() const` declaration to `LX_core::Scene` in `src/core/scene/scene.hpp`
- [x] 2.2 Implement `Scene::getSceneLevelResources()` in `src/core/scene/scene.cpp` — push camera UBO first (if `camera` and its UBO are non-null), then directional light UBO (if `directionalLight` and its UBO are non-null)
- [x] 2.3 Verify existing callers still compile (no callers yet because the method is new)

## 3. RenderQueue::buildFromScene

- [x] 3.1 Declare `void buildFromScene(const Scene &scene, StringID pass)` in `src/core/scene/render_queue.hpp`
- [x] 3.2 Implement `RenderQueue::buildFromScene` in `src/core/scene/render_queue.cpp`, moving the body of the existing `Scene::buildRenderingItemForRenderable` into an anonymous-namespace `makeItemFromRenderable(renderable, pass)` helper inside the `.cpp`
- [x] 3.3 `buildFromScene` body: `clearItems()`, retrieve `scene.getSceneLevelResources()`, iterate `scene.getRenderables()`, skip null + `!supportsPass(pass)`, construct item via helper, append scene-level resources, push to `m_items`, call `sort()` at the end
- [x] 3.4 Verify include direction: `render_queue.hpp` already includes `scene.hpp` via `RenderingItem` — no new circular includes
- [x] 3.5 Build the target — `RenderQueue::buildFromScene` now compiles but no caller uses it yet

## 4. FrameGraph switches to RenderQueue delegation

- [x] 4.1 Rewrite `FrameGraph::buildFromScene(const Scene &)` in `src/core/scene/frame_graph.cpp` to iterate `m_passes` and call `pass.queue.buildFromScene(scene, pass.name)` on each
- [x] 4.2 Delete the inline `RenderingItem` construction loop that used `scene.buildRenderingItemForRenderable(...)` from `frame_graph.cpp`
- [x] 4.3 Build the renderer target — at this point `FrameGraph::buildFromScene` is the single caller of the new path; `Scene::buildRenderingItem*` still exists for the backend and tests

## 5. VulkanRenderer rewrite

- [x] 5.1 Replace `RenderingItem renderItem{};` member in `src/backend/vulkan/vk_renderer.cpp` `Impl` with `FrameGraph m_frameGraph;`
- [x] 5.2 Add a `defaultForwardTarget()` helper on `Impl` returning a default-constructed `RenderTarget{}` (REQ-009 will replace the body with a `VkFormat → ImageFormat` derivation). Document inline that this is a placeholder for REQ-008 scope.
- [x] 5.3 Rewrite `Impl::initScene(SceneSharedPtr)`:
  - Store `scene = _scene`
  - `m_frameGraph.addPass(FramePass{Pass_Forward, defaultForwardTarget(), {}})`
  - `m_frameGraph.buildFromScene(*scene)`
  - Iterate `m_frameGraph.getPasses() × pass.queue.getItems()` to `syncResource` each item's vertex buffer, index buffer, descriptor resources, and initialize `objectInfo` push-constant
  - `resourceManager->collectGarbage()`
  - `resourceManager->preloadPipelines(m_frameGraph.collectAllPipelineBuildInfos())`
  - Keep existing `rendererDebugEnabled()` block
- [x] 5.4 **Delete** the camera/light side-channel injection block (the current lines that push `camera->getUBO()` / `directionalLight->getUBO()` into `renderItem.descriptorResources`)
- [x] 5.5 Rewrite `Impl::uploadData()` to iterate `m_frameGraph.getPasses() × pass.queue.getItems()` and `syncResource` every item's vertex/index/descriptor resources, then `collectGarbage()`
- [x] 5.6 Rewrite `Impl::draw()` so the recorded-command section iterates `m_frameGraph.getPasses() × pass.queue.getItems()` and calls `bindPipeline` / `bindResources` / `drawItem` per item, preserving the existing acquire/begin/viewport/scissor/render-pass-begin/end/submit/present structure
- [x] 5.7 Build the renderer target — expect test compile failures until step 6 migrates them
- [x] 5.8 Run `test_render_triangle` and confirm the golden triangle still renders (visual or exit-code check)

## 6. Test migration to shared helper

- [x] 6.1 Create new file `src/test/integration/scene_test_helpers.hpp` with `firstItemFromScene(Scene &scene, StringID pass)` — builds a local `RenderQueue`, calls `buildFromScene(scene, pass)`, asserts non-empty, returns `getItems().front()`
- [x] 6.2 Migrate `src/test/integration/test_vulkan_command_buffer.cpp`: include helper, replace `scene->buildRenderingItem(...)` call with `firstItemFromScene(*scene, ...)`, **delete** the "Match VulkanRenderer::initScene(): inject camera/light UBO resources" block
- [x] 6.3 Migrate `src/test/integration/test_vulkan_resource_manager.cpp` the same way
- [x] 6.4 Migrate `src/test/integration/test_vulkan_pipeline.cpp` the same way
- [x] 6.5 Migrate `src/test/integration/test_pipeline_cache.cpp` the same way (note: this file also directly uses `FrameGraph::buildFromScene` at the pipeline-preload test — that call site is not a migration target, it's already on the new path)
- [x] 6.6 Migrate `src/test/integration/test_pipeline_build_info.cpp` the same way
- [x] 6.7 Scan for any remaining side-channel UBO injection blocks with `grep -rn "camera/light UBO\|inject camera\|descriptorResources.push_back" src/test/integration/` and delete them
- [x] 6.8 Build the test suite and run `ctest --output-on-failure` for the migrated tests

## 7. Delete legacy Scene factory methods

- [x] 7.1 Remove `RenderingItem buildRenderingItem(StringID pass);` declaration from `src/core/scene/scene.hpp`
- [x] 7.2 Remove `RenderingItem buildRenderingItemForRenderable(const IRenderableSharedPtr&, StringID pass) const;` declaration from `src/core/scene/scene.hpp`
- [x] 7.3 Remove the implementations of both methods from `src/core/scene/scene.cpp`
- [x] 7.4 Verify with `grep -rn "buildRenderingItem\b" src/` that the result is empty
- [x] 7.5 Build the full project — any remaining caller triggers a hard failure

## 8. Pass-mask filter scenario in test_frame_graph.cpp

- [x] 8.1 Open `src/test/integration/test_frame_graph.cpp` and study the existing mock-renderable helpers
- [x] 8.2 Add a new scenario: build a scene with two mock renderables — A with pass mask `Forward | Shadow`, B with pass mask `Forward`. Build a `FrameGraph` with `Pass_Forward` and `Pass_Shadow`. Call `fg.buildFromScene(*scene)`. Assert `passes[0].queue.getItems().size() == 2` and `passes[1].queue.getItems().size() == 1`
- [x] 8.3 Add a second scenario asserting idempotent rebuild: call `fg.buildFromScene(*scene)` twice and verify the queue item counts are unchanged (not doubled)
- [x] 8.4 Build and run `test_frame_graph` to confirm both new scenarios pass

## 9. Banner on superseded requirement doc

- [x] 9.1 Add a "Partial supersede by this change (frame-graph-drives-rendering)" banner to `docs/requirements/finished/007-interning-pipeline-identity.md` top, scoped to R9 only. Banner text: R9's `Scene::buildRenderingItem(StringID pass)` normative entry is deprecated; `RenderingItem` construction moves to `RenderQueue::buildFromScene`. R1–R8 continue to hold.
- [x] 9.2 Do NOT modify any other sections of the finished doc

## 10. Archive requirement docs after landing

- [x] 10.1 Move `docs/requirements/008-frame-graph-drives-rendering.md` to `docs/requirements/finished/008-frame-graph-drives-rendering.md` once all R1–R8 are verified against the implementation (this is typically handled by `/finish-req` rather than by hand — tag it here as a checklist item to remember)
- [x] 10.2 Run `openspec validate frame-graph-drives-rendering --strict` and fix any issues
- [x] 10.3 Archive the openspec change via `/opsx:archive frame-graph-drives-rendering` once the deltas have landed on the live specs
