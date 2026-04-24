## ADDED Requirements

### Requirement: ImageFormat enum enumerates core-supported texel formats
The system SHALL provide `LX_core::ImageFormat`, a `uint8_t`-backed enum covering at minimum `RGBA8`, `BGRA8`, `R8`, `D32Float`, `D24UnormS8`, and `D32FloatS8`. Backends SHALL provide a translation to their native format type (e.g., `VkFormat toVkFormat(ImageFormat)` in the Vulkan backend). Core layer code SHALL NOT reference backend-specific format types.

#### Scenario: Round-trip to backend format
- **WHEN** `toVkFormat(ImageFormat::BGRA8)` is called in the Vulkan backend
- **THEN** it returns `VK_FORMAT_B8G8R8A8_UNORM` (or the equivalent project-chosen BGRA8 variant)

### Requirement: RenderTarget describes a render pass attachment set
`LX_core::RenderTarget` SHALL be a core-layer struct containing at minimum `ImageFormat colorFormat`, `ImageFormat depthFormat`, and `uint8_t sampleCount`. It SHALL expose a stable `size_t getHash() const` suitable for use as an `unordered_map` key. `RenderTarget` membership SHALL NOT be part of `PipelineKey` at this stage of the project; the field is reserved for future multi-target support.

#### Scenario: Default target
- **WHEN** a default-constructed `RenderTarget` is inspected
- **THEN** it has a reasonable default (project picks — e.g., `BGRA8 + D32Float + sampleCount=1`) that matches today's forward pass

#### Scenario: Hash distinguishes different targets
- **WHEN** two `RenderTarget` values differ only in `sampleCount`
- **THEN** their `getHash()` values SHALL differ

### Requirement: RenderQueue collects and deduplicates pipeline build infos
`LX_core::RenderQueue` SHALL provide `void addItem(RenderingItem)`, `void sort()` (ordering by `PipelineKey` to reduce pipeline switches), `const std::vector<RenderingItem> &getItems() const`, and `std::vector<PipelineBuildInfo> collectUniquePipelineBuildInfos() const`. Deduplication MUST be performed by `PipelineKey` equality; items with the same key MUST contribute exactly one `PipelineBuildInfo` to the result.

#### Scenario: Deduplication on equal keys
- **WHEN** two `RenderingItem`s with equal `pipelineKey` are added to a queue
- **THEN** `collectUniquePipelineBuildInfos()` returns exactly one `PipelineBuildInfo`

#### Scenario: Sort is stable for equal keys
- **WHEN** `sort()` is called on a queue that contains interleaved items
- **THEN** items sharing the same `pipelineKey` are contiguous in `getItems()` afterward

### Requirement: FrameGraph models one pass per output target
`LX_core::FrameGraph` SHALL contain a sequence of `FramePass` entries. `FramePass` SHALL have at least `StringID name` (matching REQ-007 pass constants `Pass_Forward` / `Pass_Shadow` / `Pass_Deferred`), `RenderTarget target`, and `RenderQueue queue`. The `name` field MUST be `StringID`, not `std::string`, to align with `Scene::buildRenderingItem(StringID pass)`.

#### Scenario: FramePass name is a StringID
- **WHEN** a `FramePass` is constructed with `Pass_Forward`
- **THEN** `pass.name == Pass_Forward` compares true and does not allocate a new string

### Requirement: FrameGraph buildFromScene populates queues per pass
`FrameGraph::buildFromScene(const Scene &)` SHALL iterate every renderable in the scene, and for each configured pass in the frame graph SHALL produce a `RenderingItem` via `scene.buildRenderingItemForRenderable(renderable, pass.name)` (or equivalent hook) and push it onto `pass.queue`. The method SHALL NOT construct pipelines itself; it only collects items.

#### Scenario: Populating a single forward pass
- **WHEN** `FrameGraph` contains exactly one `FramePass` with `name == Pass_Forward` and the scene contains one renderable
- **THEN** after `buildFromScene(scene)`, that pass's `queue` contains exactly one `RenderingItem` whose `pass == Pass_Forward`

### Requirement: FrameGraph collectAllPipelineBuildInfos deduplicates across passes
`FrameGraph::collectAllPipelineBuildInfos()` SHALL iterate every pass's `RenderQueue`, concatenate each queue's `collectUniquePipelineBuildInfos()`, and deduplicate the combined set by `PipelineKey`. The returned vector is the input to the backend's pipeline preload step.

#### Scenario: Duplicate across passes collapses to one
- **WHEN** the same mesh+material appears in two passes whose `PipelineKey` values are identical (e.g., a trivial pass remapping)
- **THEN** `collectAllPipelineBuildInfos()` returns exactly one `PipelineBuildInfo` for it

#### Scenario: Different passes keep distinct entries
- **WHEN** the same mesh+material appears under `Pass_Forward` and `Pass_Shadow` with distinct per-pass render state
- **THEN** `collectAllPipelineBuildInfos()` returns two distinct `PipelineBuildInfo` entries

### Requirement: Scene exposes a renderable collection
`LX_core::Scene` SHALL provide `const std::vector<IRenderableSharedPtr> &getRenderables() const` returning every renderable currently part of the scene. The previously single `IRenderableSharedPtr mesh` member SHALL be replaced (or wrapped) by a `std::vector<IRenderableSharedPtr> m_renderables` member so `FrameGraph::buildFromScene` can iterate.

#### Scenario: Single renderable scene
- **WHEN** a scene is constructed with one renderable (matching today's tests)
- **THEN** `getRenderables()` returns a vector with exactly one element
