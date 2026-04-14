## MODIFIED Requirements

### Requirement: VulkanRenderer shall implement complete render lifecycle

The VulkanRenderer SHALL implement:
- `initialize(WindowPtr)`: Create device, swapchain, command buffers, and resources
- `shutdown()`: Destroy all Vulkan objects in reverse creation order
- `initScene(ScenePtr)`:
  - Store the scene pointer
  - Configure the owned `FrameGraph m_frameGraph` member by calling `m_frameGraph.addPass(FramePass{Pass_Forward, /*target*/, {}})` (additional passes MAY be added in future changes)
  - Call `m_frameGraph.buildFromScene(*scene)` so every `FramePass::queue` is populated via `RenderQueue::buildFromScene(scene, pass.name)`
  - Iterate `m_frameGraph.getPasses() × pass.queue.getItems()` to sync vertex/index/descriptor resources and initialize each item's `objectInfo` push-constant
  - Call `resourceManager->preloadPipelines(m_frameGraph.collectAllPipelineBuildInfos())`
  - SHALL NOT side-channel-inject camera or light UBOs into a `RenderingItem`'s `descriptorResources`. Scene-level UBOs flow through `Scene::getSceneLevelResources()` which is merged inside `RenderQueue::buildFromScene`.
- `uploadData()`: Iterate `m_frameGraph.getPasses() × pass.queue.getItems()` and sync every item's vertex buffer, index buffer, and descriptor resources. SHALL NOT reference a cached single-item member.
- `draw()`: Acquire image, record commands, iterate `m_frameGraph.getPasses() × pass.queue.getItems()` binding the pipeline/resources and calling `cmd->drawItem(item)` for each, submit, present
- Resolving the bound graphics pipeline using `RenderingItem::pipelineKey` and the resource manager pipeline cache on each draw

The `VulkanRenderer::Impl` class SHALL hold the `FrameGraph` as a member whose lifetime matches the scene binding. The `Impl` class SHALL NOT hold a cached single `RenderingItem` member; every draw call consults the FrameGraph.

#### Scenario: Triangle rendering loop
- **WHEN** Renderer has initialized with a triangle scene containing one `RenderableSubMesh`
- **THEN** `draw()` iterates the one pass in `m_frameGraph.getPasses()`, iterates the one item in that pass's queue, and calls `acquireNextImage`, `beginCommandBuffer`, `beginRenderPass`, `bindPipeline`, `bindResources`, `drawItem`, `endRenderPass`, `endCommandBuffer`, `queueSubmit`, `present`

#### Scenario: Multi-renderable scene produces multi-item queue
- **WHEN** a scene is constructed with two `RenderableSubMesh` instances both supporting `Pass_Forward`, and `initScene(scene)` is called followed by `draw()`
- **THEN** the `draw()` loop iterates the one pass's queue and calls `cmd->drawItem(item)` exactly twice in PipelineKey-sorted order

#### Scenario: No side-channel UBO injection
- **WHEN** `initScene(scene)` is called on a scene with a non-null camera UBO and non-null directional light UBO
- **THEN** the resulting `RenderingItem`s inside `m_frameGraph.getPasses()[0].queue` carry the camera and light UBOs in their `descriptorResources` purely because `Scene::getSceneLevelResources()` was merged by `RenderQueue::buildFromScene`, and no code inside `VulkanRenderer::Impl::initScene` pushes those UBOs into the item directly

### Requirement: VulkanResourceManager shall map IRenderResource to Vulkan objects

The VulkanResourceManager SHALL support:
- Creating VulkanBuffer from IRenderResource with type VertexBuffer or IndexBuffer
- Creating VulkanTexture from IRenderResource with type CombinedImageSampler
- Maintaining map of IRenderResource* to created Vulkan objects
- Initializing render pass with correct formats
- Delegating pipeline caching to a standalone `LX_core::backend::PipelineCache` instance (see the `pipeline-cache` capability). The resource manager SHALL NOT store the pipeline map directly; the legacy `getOrCreateRenderPipeline(item)` helper, if retained, SHALL be a thin forward to `PipelineCache::getOrCreate(PipelineBuildInfo::fromRenderingItem(item), renderPass)`

#### Scenario: Resource mapping for vertex buffer
- **WHEN** `initScene` iterates `m_frameGraph.getPasses() × pass.queue.getItems()` and encounters a vertex buffer `IRenderResource`
- **THEN** VulkanResourceManager SHALL store the mapping and return a valid VulkanBuffer

#### Scenario: Pipeline lookup delegates to PipelineCache
- **WHEN** draw logic requests a pipeline for a `PipelineKey` present in the cache
- **THEN** the request resolves via `PipelineCache::find` and no code path references `VulkanResourceManager::m_pipelines` (which SHALL not exist after this change)
