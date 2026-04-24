## ADDED Requirements

### Requirement: PipelineCache is a standalone, backend-owned class
The Vulkan backend SHALL provide `LX_core::backend::PipelineCache` as a dedicated class whose sole responsibility is storing and materializing `VulkanPipeline` instances keyed by `PipelineKey`. `VulkanResourceManager` SHALL NOT own the pipeline map directly; if it exposes a `getOrCreateRenderPipeline` helper for backward compatibility, that helper SHALL delegate to `PipelineCache`.

#### Scenario: ResourceManager delegates to cache
- **WHEN** `VulkanResourceManager::getOrCreateRenderPipeline(item)` is invoked
- **THEN** the call resolves via `PipelineCache::getOrCreate(...)` and no `std::unordered_map<PipelineKey, VulkanPipelineUniquePtr>` lives on the resource manager itself

### Requirement: PipelineCache::find returns a reference without constructing
`PipelineCache::find(const PipelineKey &)` SHALL return `std::optional<std::reference_wrapper<VulkanPipeline>>`. On miss the method MUST NOT allocate or construct a pipeline; it returns `std::nullopt`. Callers that expect a built pipeline SHALL use `getOrCreate` explicitly.

#### Scenario: Cold cache miss is observable
- **WHEN** `cache.find(keyNeverSeen)` is called
- **THEN** it returns `std::nullopt` and the cache size is unchanged

### Requirement: PipelineCache::getOrCreate builds, caches, and warns on miss
`PipelineCache::getOrCreate(const PipelineBuildInfo &info, VkRenderPass renderPass)` SHALL return a `VulkanPipeline &` whose lifetime is owned by the cache. On cache hit (`info.key` already present) it returns the existing instance. On cache miss it MUST build a new `VulkanPipeline` from `info` against `renderPass`, store it in the cache, and emit a warning-level log message identifying the key (e.g., via `GlobalStringTable::toDebugString(info.key.id)`) so runtime misses are diagnosable.

#### Scenario: Miss then hit
- **WHEN** `getOrCreate(info, rp)` is called twice for the same `info.key`
- **THEN** the first call constructs a new pipeline (cache grows by 1, warning emitted); the second returns the same instance without building and without emitting a warning

### Requirement: PipelineCache::preload builds all inputs up-front without warnings
`PipelineCache::preload(const std::vector<PipelineBuildInfo> &infos, VkRenderPass renderPass)` SHALL iterate `infos` and call `getOrCreate` for each entry, with the cache-miss warning suppressed because preloading is the expected first-build path. After `preload` returns, every `info.key` in the input vector SHALL be findable via `find(...)`.

#### Scenario: Preload warms every key
- **WHEN** `preload({info1, info2, info3}, rp)` is called against an empty cache
- **THEN** `find(info1.key)`, `find(info2.key)`, `find(info3.key)` all return non-empty optionals and no miss warning is emitted

#### Scenario: Preload is idempotent
- **WHEN** `preload` is called twice with overlapping inputs
- **THEN** the second call does not rebuild already-present entries

### Requirement: FrameGraph-driven preload is the primary path
The `VulkanRenderer` or equivalent frame-loop owner SHALL, during scene initialization, build a `FrameGraph` from the scene, call `FrameGraph::collectAllPipelineBuildInfos()`, and pass the result to `PipelineCache::preload(...)`. Runtime misses (new material introduced mid-frame) SHALL still be handled by `getOrCreate` as a fallback with a warning log.

#### Scenario: initScene triggers preload
- **WHEN** `VulkanRenderer::initScene(scenePtr)` runs
- **THEN** a `FrameGraph` is built from the scene, `PipelineCache::preload(...)` is invoked, and every `RenderingItem` subsequently submitted to `draw()` resolves via `find(...)` without emitting a warning
