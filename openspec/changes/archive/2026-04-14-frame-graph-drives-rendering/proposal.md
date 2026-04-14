# Proposal: FrameGraph drives rendering

## Why

`VulkanRenderer` today bypasses `FrameGraph` entirely — it holds a single `RenderingItem renderItem{}` member, draws that one item per frame, and uses `FrameGraph` only for a one-shot pipeline preload that is then thrown away. As a result, the second and subsequent `IRenderable`s in a scene are invisible to the backend, `Pass_Shadow` / `Pass_Deferred` have no draw path even when configured, and scene-level UBOs (camera, light) must be side-channel-injected into every integration test by hand. This change rewires the code so that `FrameGraph → RenderQueue → RenderingItem` is the single data flow path the Vulkan backend consumes, matching the architecture the existing specs already claim.

## What Changes

- **BREAKING** Delete `Scene::buildRenderingItem(StringID pass)` and `Scene::buildRenderingItemForRenderable(const IRenderablePtr&, StringID pass) const`. The `RenderingItem` factory job moves to `RenderQueue`.
- Add `RenderQueue::buildFromScene(const Scene &scene, StringID pass)` as the single entry point for turning scene data into `RenderingItem`s. It internally filters by `IRenderable::supportsPass(pass)` and merges `Scene::getSceneLevelResources()` into each item's `descriptorResources`.
- Add `IRenderable::supportsPass(StringID pass)` as a virtual method with a default implementation based on `getPassMask()` and a new helper `passFlagFromStringID(StringID)` living in `src/core/scene/pass.{hpp,cpp}`.
- Add `Scene::getSceneLevelResources()` (parameterless) returning the interleaved camera UBO + directional light UBO vector. REQ-009 will later replace this with a `(pass, target)`-aware version; this change keeps the single-camera / single-light assumption.
- **BREAKING** Change `FrameGraph::buildFromScene(const Scene &)` to delegate per-pass `RenderingItem` construction to `RenderQueue::buildFromScene`, instead of calling the deleted `scene.buildRenderingItemForRenderable` hook.
- **BREAKING** Change `VulkanRenderer::Impl` to hold `FrameGraph m_frameGraph` as a member (not a throwaway local), and rewrite `initScene` / `uploadData` / `draw` to iterate `m_frameGraph.getPasses() × pass.queue.getItems()`. Delete the side-channel camera/light UBO injection block inside `initScene`.
- Migrate all 5 integration test files that currently call `scene->buildRenderingItem(...)` to a new `firstItemFromScene(scene, pass)` helper in `src/test/integration/scene_test_helpers.hpp`, and remove their "Match VulkanRenderer::initScene: inject camera/light UBO" blocks.
- Add a `pass-mask filter` scenario to `src/test/integration/test_frame_graph.cpp` that verifies a renderable with `Forward | Shadow` pass mask lands in both passes' queues while a `Forward`-only renderable is excluded from `Pass_Shadow`.

## Capabilities

### New Capabilities

_(none — this change only restructures existing capabilities)_

### Modified Capabilities

- `frame-graph`: `FrameGraph::buildFromScene` stops calling a Scene-side item factory and instead delegates to `RenderQueue::buildFromScene(scene, pass, ...)`. `RenderQueue` gains the `buildFromScene` entry point. `Scene` loses the `buildRenderingItem*` methods and gains `getSceneLevelResources()`. `IRenderable` gains `supportsPass(pass)` as the queue-side filter predicate.
- `render-signature`: Removes the `Scene::buildRenderingItem accepts a pass parameter` requirement because the method itself is deleted; the `PipelineKey::build(objSig, matSig)` invariant moves into `RenderQueue::buildFromScene` and is asserted there instead. Adds a `passFlagFromStringID(StringID)` helper requirement in the same `src/core/scene/pass.hpp` header that already hosts the `Pass_*` constants.
- `renderer-backend-vulkan`: `VulkanRenderer::initScene` now configures and owns a `FrameGraph`, and the `draw()` lifecycle iterates `frameGraph.getPasses() × pass.queue.getItems()` instead of drawing a single cached `renderItem`. The side-channel camera/light UBO injection is removed.

## Impact

**Affected source files**:
- `src/core/scene/pass.hpp` — add `passFlagFromStringID` declaration
- `src/core/scene/pass.cpp` — **NEW** file, `passFlagFromStringID` implementation
- `src/core/scene/object.hpp` — add `IRenderable::supportsPass(pass)` virtual
- `src/core/scene/scene.hpp` / `scene.cpp` — delete `buildRenderingItem*`; add `getSceneLevelResources()`
- `src/core/scene/render_queue.hpp` / `render_queue.cpp` — add `buildFromScene(scene, pass)`, move the item-construction logic out of Scene
- `src/core/scene/frame_graph.cpp` — delegate to `RenderQueue::buildFromScene`
- `src/backend/vulkan/vk_renderer.cpp` — `m_frameGraph` member, rewritten `initScene`/`uploadData`/`draw`, deleted side-channel injection
- 5 integration test files (`test_vulkan_command_buffer.cpp`, `test_vulkan_resource_manager.cpp`, `test_vulkan_pipeline.cpp`, `test_pipeline_cache.cpp`, `test_pipeline_build_info.cpp`) — migrate to new helper, delete UBO injection
- `src/test/integration/scene_test_helpers.hpp` — **NEW** helper
- `src/test/integration/test_frame_graph.cpp` — new pass-mask filter scenario + idempotent rebuild scenario

**Affected specs**:
- `openspec/specs/frame-graph/spec.md` — delta
- `openspec/specs/render-signature/spec.md` — delta
- `openspec/specs/renderer-backend-vulkan/spec.md` — delta

**Requirement source**: `docs/requirements/008-frame-graph-drives-rendering.md` (REQ-008)

**Downstream unlock**: REQ-009 (multi-camera / multi-light / `getSceneLevelResources(pass, target)`) depends on this change landing first.

**Compatibility**: All callers of `Scene::buildRenderingItem*` are in-repo tests and the Vulkan backend; no external consumers. The change is internally breaking but self-contained.
