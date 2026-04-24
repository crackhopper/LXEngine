## Context

Post-REQ-007, pipeline identity is a structured `StringID` produced by `compose(TypeTag::PipelineKey, {objSig, matSig})`, and `Scene::buildRenderingItem(StringID pass)` threads the pass all the way to `PipelineKey::build`. But the moment we leave the core layer and enter backend, the world reverts to 2024:

1. `VulkanResourceManager::getOrCreateRenderPipeline(item)` hard-switches on `item.shaderInfo->getShaderName() == "blinnphong_0"` and calls `blinnPhongForwardSlots()` to pull a hand-authored `std::vector<PipelineSlotDetails>`. Any new shader means editing `vk_resource_manager.cpp`.
2. `VulkanPipeline` stores that slot vector directly as `m_slots` and uses it to build `VkDescriptorSetLayout` via `VulkanDescriptorManager::getOrCreateLayout(slots)`. The layout key itself is `DescriptorLayoutKey{ std::vector<PipelineSlotDetails> }`.
3. `VulkanCommandBuffer::bindResources` closes the loop by iterating `pipeline.getSlots()` and calling `findBySlotId(slot.id)` to match a `PipelineSlotId` (enum value like `CameraUBO`) against `item.descriptorResources` via `IRenderResource::getPipelineSlotId()`. Every resource class — `Camera`, `Light`, `Skeleton`, `UboByteBufferResource`, `CombinedTextureSampler` — overrides this virtual.
4. There is no frame graph abstraction. `Scene::mesh` is a single `IRenderableSharedPtr`. `buildRenderingItem(Pass_Forward)` returns one `RenderingItem` at a time. The concept of "all pipelines the scene needs" doesn't exist, so neither does preloading.

What we **already have** post REQ-004/005/007:

- `IShader::getReflectionBindings()` returns `std::vector<ShaderResourceBinding>` with `{name, set, binding, type, descriptorCount, size, offset, stageFlags, members}` — everything needed to build a descriptor set layout without a hand-authored table.
- Every shader resource in the reflection comes back with a real `name` field (`"CameraUBO"`, `"Bones"`, `"albedoTex"`, etc.), because REQ-004 threaded `ShaderResourceBinding` through the reflector.
- `MaterialInstance`'s texture routing is already name-keyed via `StringID` (`m_template->findBinding(id)`) — we don't even need to migrate that part.
- `Scene::buildRenderingItem(StringID pass)` exists and is per-pass.

So the shape of the work is: wrap the reflection-driven data we already produce in a `PipelineBuildInfo`, restructure the backend to consume it, delete the parallel `PipelineSlotDetails` universe, and introduce a frame-graph that multiplies `buildRenderingItem` across passes and renderables.

## Goals / Non-Goals

**Goals:**

- `PipelineBuildInfo` as the single core-layer packet for pipeline construction; `fromRenderingItem` factory pulls everything from shader reflection
- Backend `VulkanPipeline` / descriptor manager / command buffer paths are reflection-driven end-to-end; `PipelineSlotId`, `PipelineSlotDetails`, and `blinnPhongForwardSlots()` **deleted**
- Independent `PipelineCache` class: `find` / `getOrCreate` / `preload`; `VulkanResourceManager` stops owning the pipeline map
- `FrameGraph` + `RenderQueue` + `RenderTarget` + `ImageFormat` as a minimal frame structure; `Scene::getRenderables()` + a `std::vector<IRenderableSharedPtr>` container internally
- Load-time preloading: `VulkanRenderer::initScene(scene)` builds a `FrameGraph`, collects pipeline build infos, and primes the cache; runtime misses still work but emit warnings
- `test_render_triangle` continues to render a triangle; new focused tests exercise `PipelineBuildInfo`, `FrameGraph`, and `PipelineCache` without requiring an actual GPU (using fakes where possible)

**Non-Goals:**

- Not introducing `VkPipelineCache` (native Vulkan PSO cache on disk). That's a separate optimization.
- Not building a real multi-pass pipeline (shadow maps, deferred). Only the abstractions; the project still ships one `Pass_Forward` pass in practice.
- Not rewriting descriptor set allocation strategy or lifetime (frame-scoped allocation from `VulkanDescriptorManager` remains).
- Not changing push constant layout (`PC_Base`/`PC_Draw`). Push constant range is a constant for this phase.
- Not touching `BUILD_* hook` tests, window system, swapchain, etc.

## Decisions

### Decision 1: Replace `PipelineSlotId` with `StringID getBindingName()` on `IRenderResource`

**Choice**: Add `virtual StringID getBindingName() const { return StringID{}; }` to `IRenderResource`. Delete `virtual PipelineSlotId getPipelineSlotId() const`. Each concrete resource that used to return a slot enum now returns a `StringID` matching its shader-side binding name:

| Resource | Old `PipelineSlotId` | New `getBindingName()` |
|---|---|---|
| `Camera` | `CameraUBO` | `StringID("CameraUBO")` |
| `DirectionalLight` | `LightUBO` | `StringID("LightUBO")` |
| `SkeletonUBO` | `SkeletonUBO` | `StringID("Bones")` |
| `UboByteBufferResource` (material) | `MaterialUBO` | `StringID("MaterialUBO")` |
| `CombinedTextureSampler` | `AlbedoTexture`/`NormalTexture` | (already keyed in `MaterialInstance::m_textures` by `StringID`) |

**Rationale**: Names are already the natural key in reflection. A `StringID` compare is two integer reads; it's as fast as enum compare. This is what `MaterialInstance` already does for textures — we just extend the same convention to the UBO slots.

**Alternatives considered**:
- (a) Keep `PipelineSlotId` but auto-populate it from a reflection pass. Rejected — the enum is a fixed set; it can't represent a new shader's new binding.
- (b) Match by `(set, binding)` tuple instead of by name. Rejected — that's what the shader layout says anyway, but routing "this camera UBO to set X binding Y" at the binding layer would require each resource to know its set/binding numerically, which ties core to shader layout choices.

**Warning for CombinedTextureSampler**: `m_slotId` currently distinguishes `AlbedoTexture` / `NormalTexture` *per instance*. The `MaterialInstance::m_textures` map is already `unordered_map<StringID, CombinedTextureSamplerSharedPtr>` — the texture's own `getBindingName()` is redundant when routed through the material path, so we delete `m_slotId` from `CombinedTextureSampler` and return `StringID{}` (or derive it lazily). `MaterialInstance::getDescriptorResources()` already orders textures by `(set, binding)`; that sort stays.

### Decision 2: `VulkanPipeline` stores `std::vector<ShaderResourceBinding>` instead of `PipelineSlotDetails`

**Choice**: `VulkanPipeline::m_slots` becomes `std::vector<ShaderResourceBinding> m_bindings`. `VulkanShaderGraphicsPipeline::create` changes to `create(VulkanDevice &, const PipelineBuildInfo &, VkRenderPass)`. The old `extent` parameter is dropped (dynamic viewport/scissor already handles it).

`VulkanPipeline::createLayout` builds `VkDescriptorSetLayoutBinding`s from `m_bindings` grouped by `set`:

```cpp
for (const auto &b : m_bindings) {
  auto &group = groups[b.set];
  group.push_back({
    .binding = b.binding,
    .descriptorType = toVkDescriptorType(b.type),
    .descriptorCount = b.descriptorCount,
    .stageFlags = toVkStageFlags(b.stageFlags),
  });
}
for (auto &[setIdx, vec] : groups) {
  m_setLayouts.push_back(createLayout(vec));
}
```

### Decision 3: `VulkanDescriptorManager` layout key migrates to `std::vector<ShaderResourceBinding>`

Today: `DescriptorLayoutKey { std::vector<PipelineSlotDetails> slots; }` with a hash built over `(id, type, stage, setIndex, binding, size)`.

After: `DescriptorLayoutKey { std::vector<ShaderResourceBinding> bindings; }` with a hash built over `(set, binding, type, stageFlags, descriptorCount)`. The `name` field is *not* hashed — two shaders that declare compatible layouts with different member names should share the layout.

Alternative considered: key by `{uint32_t set; vector<(binding, type, stageFlags)>}` tuples — less churn in `DescriptorLayoutKey` but requires duplicating a mini struct. Staying with `ShaderResourceBinding` is simpler.

### Decision 4: `PipelineCache` is a thin owner, not a registry

**Signature**:

```cpp
class PipelineCache {
public:
  explicit PipelineCache(VulkanDevice &device);

  std::optional<std::reference_wrapper<VulkanPipeline>>
  find(const PipelineKey &key) const;

  VulkanPipeline &getOrCreate(const PipelineBuildInfo &info,
                              VkRenderPass renderPass);

  void preload(const std::vector<PipelineBuildInfo> &infos,
               VkRenderPass renderPass);

private:
  VulkanDevice &m_device;
  std::unordered_map<PipelineKey, VulkanPipelineUniquePtr, PipelineKey::Hash> m_cache;
  bool m_suppressMissWarning = false;  // toggled during preload
};
```

`preload` flips `m_suppressMissWarning` so the "cold miss is fine" case doesn't spam logs. Runtime misses outside preload emit a warning naming the key via `GlobalStringTable::toDebugString(key.id)`.

### Decision 5: `Scene` from single `IRenderableSharedPtr mesh` to `std::vector<IRenderableSharedPtr> m_renderables`

The project's one test scene today constructs `Scene::create(renderablePtr)`. The factory stays for backward compat but stores the arg in `m_renderables[0]`. `Scene::mesh` as a public field is replaced by `getRenderables()`. Any remaining direct `scene->mesh->...` access sites need migrating. (Spot-checked: `scene.cpp:buildRenderingItem` accesses `mesh` directly; will migrate to iterate — but keep the "one item" path for now.)

### Decision 6: `FrameGraph::buildFromScene` needs a per-renderable rendering-item hook

The current `Scene::buildRenderingItem(pass)` assumes "the scene has one renderable." With `std::vector<IRenderableSharedPtr>`, `FrameGraph::buildFromScene` needs to produce one `RenderingItem` per (renderable × pass) combination. We add a helper:

```cpp
// scene.hpp
RenderingItem Scene::buildRenderingItemForRenderable(const IRenderableSharedPtr &r,
                                                     StringID pass) const;
```

and `buildRenderingItem(pass)` becomes `buildRenderingItemForRenderable(m_renderables[0], pass)` for backward compat.

`FrameGraph::buildFromScene(const Scene &)`:

```cpp
for (auto &pass : m_passes) {
  pass.queue.clearItems();
  for (auto &r : scene.getRenderables()) {
    pass.queue.addItem(scene.buildRenderingItemForRenderable(r, pass.name));
  }
  pass.queue.sort();
}
```

### Decision 7: RenderTarget is present but inert

`RenderTarget` hash is **not** part of `PipelineKey` in this change. The sole forward render pass today uses a single color/depth format combo; we don't need multi-target support. `FramePass::target` exists so the abstraction is shape-correct, but the backend's preload step uses a single `VkRenderPass` (the one `VulkanResourceManager::m_renderPass` already wraps) for every pipeline it builds.

When multi-target becomes real (future change), we'd extend `PipelineBuildInfo` with a `renderTargetHash` field and plumb different `VkRenderPass`es through `preload`. Out of scope today.

### Decision 8: `PipelineBuildInfo::fromRenderingItem` extracts `renderState` from the item's material

Today `RenderingItem` doesn't carry `RenderState` directly; it carries `item.shaderInfo` (only the shader) and `item.descriptorResources` (resources). `getRenderState` is on `IMaterial` but the material isn't reachable from `RenderingItem` (we threw it away during the current `Scene::buildRenderingItem` which captures the material into `sub->material` but doesn't propagate it).

**Fix**: `RenderingItem` gains a `MaterialPtr material` field (non-owning-ish; materials are `shared_ptr`). `Scene::buildRenderingItem` populates it. `PipelineBuildInfo::fromRenderingItem` then calls `item.material->getRenderState()` and `item.material->getShaderProgramSet()` to fetch `renderState` + `shaderSet` (and thus `topology` from the index buffer).

**Alternative**: Add the whole `RenderPassEntry` to `RenderingItem`. Rejected — `material` pointer is tighter.

### Decision 9: Staged implementation, single change

tasks.md is organized by phase, matching the 1/2/3 structure in REQ-003b's "建议的实施顺序". Stage 1 and 2 are purely additive and can be committed in isolation (zero behavior change). Stage 3 is the breaking wave; it lands atomically because `PipelineSlotId` deletion fans out to every resource class. Apply phase should pause between stages for `cmake --build ./build` to verify green before continuing.

### Decision 10: New tests use fakes, not GPU init

`test_pipeline_build_info.cpp` and `test_frame_graph.cpp` run without a Vulkan device (reuse `FakeShader` pattern from `test_pipeline_identity.cpp`). `test_pipeline_cache.cpp` is the borderline case — it needs a real `VulkanDevice` to call `vkCreateGraphicsPipelines`. We either (a) make it a GPU test like `test_vulkan_pipeline.cpp` (initialize a device, build a real pipeline, verify find/getOrCreate semantics) or (b) extract a `PipelineBuilder` interface that `PipelineCache` holds as a `std::function<VulkanPipeline &(...)>` so tests can inject a fake. **Pick (a)** for this change — closer to existing `test_vulkan_pipeline` style, lower abstraction cost. Revisit if the test becomes flaky on headless CI.

## Risks / Trade-offs

- **[Risk] `PipelineSlotId` deletion ripples into every resource class** — 6+ files touched atomically.
  **Mitigation**: Stage 3 is one commit. Build after every edit in a tight loop. `grep -rn "PipelineSlotId\|getPipelineSlotId" src/` before and after should show delta = everything gone.

- **[Risk] Descriptor layout hash collision after key type change** — if the new `ShaderResourceBinding`-based hash weakly distinguishes layouts, two semantically different layouts could collapse.
  **Mitigation**: Include `(set, binding, type, stageFlags, descriptorCount)` in the hash. Add a unit test `test_descriptor_layout_key.cpp` that builds two layouts differing only in `descriptorCount` and verifies different hashes.

- **[Risk] `VulkanCommandBuffer::bindResources` name matching is O(N×M)** — N descriptors × M resources per draw. Today's draw has ~5 descriptors × ~5 resources = trivial, but a scan-per-descriptor still bothers me.
  **Mitigation**: Build a `unordered_map<StringID, IRenderResourcePtr>` once at the top of `bindResources` from `item.descriptorResources`. Lookups become O(1). Code diff is minimal.

- **[Risk] `CombinedTextureSampler::m_slotId` migration is subtle** — that field is `PipelineSlotId::AlbedoTexture` / `NormalTexture` today, and it's set by whatever loader creates the texture. If we delete the field, loaders (`blinnphong_material_loader`) need updating.
  **Mitigation**: `blinnphong_material_loader.cpp` doesn't actually set `m_slotId` directly — it goes through `MaterialInstance::setTexture(StringID, ...)` which is name-keyed already. So the field is *vestigial*. Just delete.

- **[Risk] `Scene::mesh` public field is accessed directly by vulkan command buffer test** — `scene->mesh->...`
  **Mitigation**: Migrate those sites to `scene->getRenderables().front()` or equivalent. Low count.

- **[Trade-off] `RenderingItem` gains a `MaterialPtr material` field** — slightly fatter item. But it's a shared_ptr copy, and it makes `PipelineBuildInfo::fromRenderingItem` self-contained. Worth it.

- **[Trade-off] `getBindingName()` on `IRenderResource`** — adds one virtual function to a base class touched by every resource. But it replaces one that already existed (`getPipelineSlotId`), so the total vtable shape is unchanged.

- **[Risk] Test `test_vulkan_resource_manager.cpp` may break** — it calls `resourceManager->getOrCreateRenderPipeline(item)` directly. After the migration that method is either deleted or becomes a delegator; either way the test should still compile.
  **Mitigation**: Keep `getOrCreateRenderPipeline(item)` as a thin wrapper `return m_pipelineCache.getOrCreate(PipelineBuildInfo::fromRenderingItem(item), m_renderPass->getHandle());` so the test works unchanged.

- **[Risk] `VulkanShaderGraphicsPipeline` has a `getShaderName()` override that `VulkanResourceManager` (old path) read via `item.shaderInfo->getShaderName()`** — after refactor, `PipelineBuildInfo` doesn't need shader name at all for construction, but the backend may still want it for logging / VkPipelineCache keys. Keep it as metadata, not as a lookup key.

## Migration Plan

### Stage 1 — data-only, additive

1. `core/resources/pipeline_build_info.hpp` + `.cpp` with `PipelineBuildInfo` struct + `fromRenderingItem` factory
2. `core/gpu/image_format.hpp` with `ImageFormat` enum
3. `core/gpu/render_target.hpp` + `.cpp` with `RenderTarget` struct + `getHash()`
4. Backend: `toVkFormat(ImageFormat)` helper in `vk_resource_manager.cpp`
5. Test: `test_pipeline_build_info.cpp` — construct a `RenderingItem` using `FakeShader` reflection, call `fromRenderingItem`, assert field equality
6. Build + run — nothing else should change

### Stage 2 — frame structure, Scene surgery

1. `core/scene/render_queue.hpp` + `.cpp`
2. `core/scene/frame_graph.hpp` + `.cpp`
3. `Scene::m_renderables` vector, `Scene::getRenderables()`, `Scene::buildRenderingItemForRenderable(r, pass)` — factor out of `buildRenderingItem(pass)` which becomes `buildRenderingItemForRenderable(m_renderables[0], pass)`
4. `RenderingItem::material` new field; `buildRenderingItem` populates it
5. Test: `test_frame_graph.cpp` — build a one-pass frame graph, run `buildFromScene` + `collectAllPipelineBuildInfos`, verify deduplication
6. Build + run — existing tests still pass

### Stage 3 — the big rewrite

1. `IRenderResource::getBindingName() const` virtual added; default `StringID{}`
2. Per-resource overrides: `Camera`, `DirectionalLight`, `SkeletonUBO`, `UboByteBufferResource` return named `StringID`s; `CombinedTextureSampler::m_slotId` field removed (default `StringID{}`)
3. Delete `enum class PipelineSlotId` and `IRenderResource::getPipelineSlotId()`
4. `VulkanPipeline::m_slots` → `m_bindings: std::vector<ShaderResourceBinding>`; constructor takes `PipelineBuildInfo`
5. `VulkanShaderGraphicsPipeline::create(device, buildInfo, renderPass)`; old `shaderBaseName/vertexLayout/slots/pushConstants/topology` signature deleted
6. `VulkanPipeline::createLayout()` uses `m_bindings` grouped by `set`
7. `VulkanDescriptorManager::getOrCreateLayout(std::span<const ShaderResourceBinding>)` + `allocateSet(same)` + `DescriptorLayoutKey` migration
8. `VulkanCommandBuffer::bindResources` builds `unordered_map<StringID, IRenderResourcePtr>` from `item.descriptorResources`, iterates `pipeline.getBindings()` and matches by name
9. Delete `vkp_pipeline_slot.hpp` + `forward_pipeline_slots.hpp` + `blinnPhongForwardSlots()` + `blinnPhongPushConstants()`
10. `PipelineCache` class new files; migrate `VulkanResourceManager::m_pipelines` into it
11. `VulkanRenderer::initScene` builds a `FrameGraph`, calls `PipelineCache::preload(...)`
12. Test: `test_pipeline_cache.cpp` (GPU — verify preload, find, getOrCreate)
13. Full regression: `test_render_triangle` still draws a triangle; all `test_vulkan_*` pass

**Rollback strategy**: Revert the three staged commits (or the one mega-commit) individually. Stage 1 and 2 are pure additions; Stage 3 is the single destructive wave — it's the only one that would cause pain to revert, and it should be a single commit for that reason.

## Open Questions

- **Q1**: Should `PipelineCache` own `VkRenderPass` resolution (look up from an injected frame graph) or receive the render pass handle at `preload` / `getOrCreate` call time?
  **Tentative**: Receive at call time. Preload is called from `VulkanRenderer::initScene` which already owns the render pass. Cleaner than injecting another indirection.

- **Q2**: Does the `bindings` hash in `DescriptorLayoutKey` need to ignore binding declaration order?
  **Tentative**: No — Vulkan treats `VkDescriptorSetLayoutCreateInfo::pBindings` as unordered by binding number internally, but the hash is easier to compute with a sort-before-hash. If we hit a false miss, add sorting.

- **Q3**: What happens if a `RenderingItem`'s `material->getRenderState()` returns a stale state (a `MaterialInstance` whose template was mutated after the item was built)?
  **Answer**: `PipelineBuildInfo::fromRenderingItem` is called once per scene load; mid-frame template mutation is not a supported workflow today. Add an `assert(item.material)` and rely on the invariant.

- **Q4**: Does `FrameGraph::buildFromScene` need to handle renderables that have their own opinion about which passes they participate in (`renderable.getPassMask()`)?
  **Tentative**: Yes, but trivially — skip a renderable for a pass if `getPassMask() & passFlag == 0`. This preserves today's `ResourcePassFlag` semantics without inventing a new one.
