# Design: FrameGraph drives rendering

## Context

The codebase currently has two parallel item-construction paths and a `VulkanRenderer` that uses neither correctly:

1. **`Scene::buildRenderingItem(StringID pass)`** — a single-item shortcut that treats the scene as if it has exactly one renderable (`m_renderables[0]`). Used directly by `VulkanRenderer::initScene` and by every integration test that needs "a valid `RenderingItem`".
2. **`Scene::buildRenderingItemForRenderable(const IRenderableSharedPtr &, StringID pass)`** — the per-renderable factory, called from `FrameGraph::buildFromScene` as it iterates the scene's renderables × passes.

`FrameGraph` is constructed in `VulkanRenderer::initScene` only for the purpose of enumerating `PipelineBuildInfo`s for cache preload — the resulting `FrameGraph` is immediately dropped. The actual draw loop uses a cached `RenderingItem renderItem{}` member, side-channel-injected with camera and light UBOs by hand. The side-channel injection is replicated across five integration tests with comments like `"Match VulkanRenderer::initScene(): inject camera/light UBO resources"`, which is the bright-red test smell that first pointed at this architectural hole.

The `frame-graph` capability spec already says `FrameGraph` is the driver. The implementation disagrees. This change makes the implementation match the spec.

## Goals / Non-Goals

**Goals:**
- `FrameGraph` becomes a member of `VulkanRenderer::Impl`, lives for the scene's lifetime, and its `getPasses() × pass.queue.getItems()` is the only source of draws.
- `RenderQueue` gains ownership of `RenderingItem` construction via `buildFromScene(scene, pass)`. Delete both `Scene::buildRenderingItem*` methods.
- Scene-level resources (camera UBO, light UBO) flow through `Scene::getSceneLevelResources()` and are merged inside `RenderQueue::buildFromScene`, so no test needs to replicate side-channel injection.
- `IRenderable::supportsPass(pass)` gives the queue a per-renderable filter predicate, removing the "we silently draw every renderable in every pass" bug.
- All five integration tests migrate cleanly via a single `firstItemFromScene` helper, and delete their inline UBO injection blocks.

**Non-Goals:**
- Multi-camera / multi-light / `Scene::getSceneLevelResources(pass, target)` — deferred to REQ-009 entirely.
- Camera owning a `RenderTarget`, `LightBase` abstract interface, `DirectionalLight` with pass mask — all REQ-009.
- Implementing a real `Pass_Shadow` or `Pass_Deferred` draw path — this change only addresses the architecture; `initScene` still hard-codes `addPass(Pass_Forward)`. The pass-mask filter test (`test_frame_graph.cpp`) validates the plumbing via mock renderables without touching the Vulkan backend.
- Dynamic scene mutation — `FrameGraph::buildFromScene` is still called once during `initScene`. Runtime add/remove of renderables with automatic queue rebuild is a downstream concern.
- Replacing the bit-flag `ResourcePassFlag` with `StringID` — the `passFlagFromStringID` helper keeps the two representations coexisting, translating at the boundary.

## Decisions

### D1: `RenderingItem` construction lives in `RenderQueue`, not `Scene`

**Decision**: Move the `makeItemFromRenderable(renderable, pass)` helper — currently the body of `Scene::buildRenderingItemForRenderable` — into `render_queue.cpp` as an anonymous-namespace free function. `RenderQueue::buildFromScene` calls it per renderable.

**Alternatives considered**:
- **Keep it in `Scene` as a private method** and have `RenderQueue` call `scene.makeItemFromRenderable(...)`. Rejected: keeps the coupling inverted (the queue still depends on Scene internals), and the Scene class becomes more complex than a pure data container.
- **Put it on `IRenderable` as `virtual RenderingItem toItem(pass) const`**. Rejected: `RenderingItem` construction needs `sub->material->getRenderSignature(pass)` to compute `pipelineKey`, which would make every `IRenderable` implementation carry pipeline-identity-construction logic — leaky.

**Rationale**: `RenderQueue` is the natural owner because it's the aggregate that holds items anyway. `Scene` becomes a pure data container. Future multi-queue / multi-target support (REQ-009) will extend `RenderQueue::buildFromScene` without having to chase through Scene.

### D2: `IRenderable::supportsPass(pass)` is a virtual method with a default implementation

**Decision**: Add `virtual bool supportsPass(StringID pass) const` with a default implementation using `getPassMask()` and a new helper `passFlagFromStringID(StringID)`. Subclasses (e.g. `RenderableSubMesh`) do not need to override.

```cpp
virtual bool supportsPass(StringID pass) const {
    const auto flag = passFlagFromStringID(pass);
    return (static_cast<uint32_t>(getPassMask()) &
            static_cast<uint32_t>(flag)) != 0;
}
```

**Alternatives considered**:
- **Keep using `getPassMask()` directly in `RenderQueue::buildFromScene`**. Rejected: couples the queue to the bit-flag representation and makes it impossible for a future `IRenderable` subclass (e.g., material-driven pass participation based on whether the template has a `Pass_Shadow` entry) to override.
- **Make `supportsPass` pure virtual**. Rejected: forces every subclass to reimplement the obvious bit-test; the default is correct 99% of the time.

**Rationale**: Virtual with default keeps the fast path simple and cheap, but leaves the override door open for future subclasses that need smarter filtering.

### D3: `passFlagFromStringID` lives in `scene/pass.hpp`, not in `gpu/render_resource.hpp`

**Decision**: The helper connecting pass `StringID` to `ResourcePassFlag` lives in `src/core/scene/pass.{hpp,cpp}`, the same header that defines `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow`.

**Alternative considered**: Put it next to `ResourcePassFlag` in the GPU-resource layer.

**Rationale**: The helper bridges scene-layer pass identity (`StringID`) with resource-layer pass flag (`ResourcePassFlag`). Placing it in `scene/pass.hpp` keeps the dependency direction clean: `scene` depends on `gpu/render_resource.hpp` for the flag type, but `gpu` doesn't gain a dependency back into `scene` for `StringID` constants. Also co-locating with the `Pass_*` constants makes the translation table discoverable.

### D4: `Scene::getSceneLevelResources()` is **parameterless** in this change

**Decision**: REQ-008 keeps the single-camera / single-light assumption. The method signature is `std::vector<IRenderResourcePtr> getSceneLevelResources() const`, returning `{camera->getUBO(), directionalLight->getUBO()}` when non-null.

**Alternative considered**: Take `(pass, target)` parameters up front to avoid the API break when REQ-009 lands.

**Rationale**: REQ-008's scope is explicitly "fix the data flow" and REQ-009's scope is "add multi-camera/multi-light filtering". Shipping the `(pass, target)` signature now but ignoring the parameters would be a half-finished implementation that lies about its filter semantics. Better to take the API break when REQ-009 actually has filtering logic behind it.

### D5: Scene-level resources merged **inside** `RenderQueue::buildFromScene`, not at the draw site

**Decision**: `RenderQueue::buildFromScene` retrieves `scene.getSceneLevelResources()` once before the loop, then appends them to each item's `descriptorResources` before pushing into the queue.

**Alternatives considered**:
- **Merge at draw time in `VulkanCommandBuffer::bindResources`**. Rejected: forces the backend to understand "scene-level" vs "item-level" resources, and every backend (not just Vulkan) would need to replicate the logic.
- **Have `RenderingItem` hold a `scene_level_resources` pointer**. Rejected: adds a field that only exists to support this one flow; leaks aggregation detail into the per-item struct.

**Rationale**: Merging at build time means `RenderingItem` stays a flat, self-contained record and backends never need to distinguish scene-level from item-level resources. The cost is one extra `vector::insert` per item, which is cheap at current queue sizes.

**Ordering**: Scene-level resources are appended **after** the renderable's own `descriptorResources`, matching the original `VulkanRenderer::initScene` injection order (renderable UBOs first, then camera, then light). This keeps the descriptor set binding indices stable and avoids a shader-side regression.

### D6: `FramePass.target` is a placeholder in REQ-008

**Decision**: `VulkanRenderer::initScene` calls a `defaultForwardTarget()` helper that MAY return a default-constructed `RenderTarget{}`. No code path in REQ-008 reads `pass.target` to filter anything — the field exists only for future REQ-009 consumption.

**Rationale**: REQ-008's job is data-flow repair. Adding a real `VkFormat → ImageFormat` translation now would mean writing code that nothing consumes. REQ-009 will implement `makeSwapchainTarget()` properly with the full format translation, because it's the first change that actually reads `pass.target`. This "placeholder now, real value later" is explicitly recorded in REQ-008's boundary constraints.

### D7: Test migration goes through a single `firstItemFromScene` helper

**Decision**: Create `src/test/integration/scene_test_helpers.hpp` with one helper:

```cpp
inline LX_core::RenderingItem
firstItemFromScene(LX_core::Scene &scene, LX_core::StringID pass) {
    LX_core::RenderQueue q;
    q.buildFromScene(scene, pass);
    assert(!q.getItems().empty() && "scene produced no items for pass");
    return q.getItems().front();
}
```

All five migrated tests call this single function.

**Alternative considered**: Inline `RenderQueue q; q.buildFromScene(...); q.getItems().front();` in each test.

**Rationale**: One helper, five callers. REQ-009 will extend the signature with a `target` parameter — updating one helper is one edit; updating five inlined call sites is five edits. The `assert` also standardizes the "scene produced no items" failure mode, which is exactly the kind of silent test bug we just found in production code.

## Risks / Trade-offs

- **[Risk]** `initScene`'s FrameGraph setup hard-codes `addPass(Pass_Forward)`, so the new draw loop has exactly one pass to iterate. Any subtle loop bug is indistinguishable from "it worked before".
  → **Mitigation**: R8's new `test_frame_graph.cpp` pass-mask filter scenario uses mock renderables to validate the multi-pass loop without needing a real Vulkan backend. If the FrameGraph iteration logic is broken, this unit test fails even though `test_render_triangle` still passes.

- **[Risk]** Deleting `Scene::buildRenderingItem*` is an API break that will cascade to any caller the grep missed.
  → **Mitigation**: Verify via `grep -rn "buildRenderingItem\b" src/` returning zero hits after R5 lands. The deletion is guarded by the integration-test migration in R7 — if a caller is missed, the build breaks loudly.

- **[Risk]** `FramePass.target` placeholder value could accidentally be consumed by some code path we missed.
  → **Mitigation**: `grep -rn "FramePass.*target\|pass\.target" src/ --include='*.cpp' --include='*.hpp'` should return only the write site (in `initScene`) after this change, not any reads. If a read is found, the REQ-008 boundary is violated and we either defer the read to REQ-009 or implement `toImageFormat` in this change.

- **[Risk]** `IRenderable::supportsPass` default implementation depends on `getPassMask()` returning the right bits for the current behavior. If any subclass currently returns `0` or `~0` as a placeholder, the filter will either exclude everything or include everything.
  → **Mitigation**: `grep -rn "getPassMask" src/core/scene/ src/infra/` to audit all returns; add an explicit `static_assert`-style debug check in `RenderableSubMesh::RenderableSubMesh` if a default-constructed mask would break the filter.

- **[Risk]** REQ-007's R9 spec requirement explicitly declares `Scene::buildRenderingItem(StringID pass)` as the normative entry. Deleting it contradicts the still-archived spec.
  → **Mitigation**: Mark REQ-007 R9 as superseded by REQ-008 in the finished requirement doc (adding a "Partial supersede by REQ-008" banner is allowed by project convention). The openspec `render-signature` delta in this change removes the corresponding requirement cleanly.

- **[Trade-off]** Moving `RenderingItem` construction into `RenderQueue` means the queue now depends on `Scene` (for `getRenderables()` / `getSceneLevelResources()`). `render_queue.hpp` already includes `scene.hpp` indirectly via `RenderingItem`, so no new include cycle is introduced — but the direction of dependency is now explicit.

## Migration Plan

1. Land `passFlagFromStringID` helper + `IRenderable::supportsPass` virtual (R1, R2) — additive, no breakage.
2. Land `Scene::getSceneLevelResources()` parameterless version (R3) — additive.
3. Land `RenderQueue::buildFromScene(scene, pass)` (R4) — additive, but `FrameGraph::buildFromScene` still calls the old Scene-side factory at this point.
4. Switch `FrameGraph::buildFromScene` over to `RenderQueue::buildFromScene` — now two paths exist briefly; the old Scene-side path is no longer called from `FrameGraph` but still exists for `VulkanRenderer::initScene` and tests.
5. Land the `VulkanRenderer::Impl` rewrite (R6) — switches `initScene`/`uploadData`/`draw` to the FrameGraph path. Side-channel UBO injection block is deleted.
6. Land the test migration (R7) — every test now calls `firstItemFromScene` instead of `scene->buildRenderingItem`.
7. Delete `Scene::buildRenderingItem` / `buildRenderingItemForRenderable` (R5) — now safe because no callers remain.
8. Land `test_frame_graph.cpp` pass-mask filter + idempotent rebuild scenarios (R8).
9. Update REQ-007's finished doc with the "Partial supersede by REQ-008" banner.

Each step leaves the tree building. The ordering lets a hypothetical bisect pinpoint which step introduces a regression.

**Rollback**: Revert the change commit. Because `Scene::buildRenderingItem*` are restored on revert and `VulkanRenderer::initScene` returns to its pre-change form, the rollback is a single-commit revert with no data-migration concerns.

## Open Questions

- **`toImageFormat` scope**: REQ-008 explicitly defers `VkFormat → ImageFormat` to REQ-009. If implementation finds that `defaultForwardTarget()` returning `RenderTarget{}` causes `RenderTarget::operator==` or `getHash()` to misbehave downstream, we may need to promote the minimal format translation into this change. Confirm during implementation that nothing else consumes `pass.target` between REQ-008 landing and REQ-009 starting.
- **`IRenderable` include graph**: Adding `supportsPass` to `object.hpp` requires the header to see `passFlagFromStringID` and `StringID`. `pass.hpp` transitively includes `gpu/render_resource.hpp` for `ResourcePassFlag`. Verify during implementation that `object.hpp` doesn't pull in a heavy include chain; if it does, consider forward-declaring and moving the default `supportsPass` body into `object.cpp` (but then the default can't be inline).
