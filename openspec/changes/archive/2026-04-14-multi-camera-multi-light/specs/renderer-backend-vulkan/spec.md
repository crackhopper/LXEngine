## ADDED Requirements

### Requirement: toImageFormat translates VkFormat to core ImageFormat
The Vulkan backend SHALL provide `LX_core::ImageFormat toImageFormat(VkFormat)` as the reverse of `VkFormat toVkFormat(LX_core::ImageFormat)`. The translation is the backend's side of the core/backend format boundary — core code SHALL NOT reference `VkFormat` and SHALL NOT need to know about this function.

The mapping SHALL cover at minimum:
- `VK_FORMAT_B8G8R8A8_SRGB` → `ImageFormat::BGRA8`
- `VK_FORMAT_B8G8R8A8_UNORM` → `ImageFormat::BGRA8`
- `VK_FORMAT_R8G8B8A8_SRGB` → `ImageFormat::RGBA8`
- `VK_FORMAT_R8G8B8A8_UNORM` → `ImageFormat::RGBA8`
- `VK_FORMAT_D32_SFLOAT` → `ImageFormat::D32Float`
- `VK_FORMAT_D24_UNORM_S8_UINT` → `ImageFormat::D24UnormS8`
- `VK_FORMAT_D32_SFLOAT_S8_UINT` → `ImageFormat::D32FloatS8`

For any other input `VkFormat`, the function MAY return a default (`ImageFormat::RGBA8`) and log a debug warning. It SHALL NOT throw — `initScene` must be robust against driver-specific surface format choices.

#### Scenario: Round-trip BGRA8
- **WHEN** `toImageFormat(VK_FORMAT_B8G8R8A8_SRGB)` is called
- **THEN** the returned value is `LX_core::ImageFormat::BGRA8`, and `toVkFormat` of that value returns a BGRA8-family VkFormat

#### Scenario: Round-trip depth
- **WHEN** `toImageFormat(VK_FORMAT_D32_SFLOAT)` is called
- **THEN** the returned value is `LX_core::ImageFormat::D32Float`

## MODIFIED Requirements

### Requirement: VulkanRenderer shall implement complete render lifecycle

The VulkanRenderer SHALL implement:
- `initialize(WindowSharedPtr)`: Create device, swapchain, command buffers, and resources
- `shutdown()`: Destroy all Vulkan objects in reverse creation order
- `initScene(SceneSharedPtr)`:
  - Store the scene pointer
  - Derive the swapchain `RenderTarget` via a backend helper `makeSwapchainTarget()` that reads `device->getSurfaceFormat()` and `device->getDepthFormat()` and converts via `toImageFormat(VkFormat)`. The resulting `RenderTarget` SHALL have a real non-default `colorFormat` and `depthFormat`.
  - Before building the frame graph, iterate `scene->getCameras()` and call `cam->setTarget(swapchainTarget)` on every camera whose `getTarget().has_value() == false`. This backfill SHALL happen before `m_frameGraph.buildFromScene(*scene)`.
  - Reset `m_frameGraph` to a fresh instance on each `initScene` call, then call `m_frameGraph.addPass(FramePass{Pass_Forward, swapchainTarget, {}})` (additional passes MAY be added in future changes).
  - Call `m_frameGraph.buildFromScene(*scene)` so every `FramePass::queue` is populated via `RenderQueue::buildFromScene(scene, pass.name, pass.target)`.
  - Iterate `m_frameGraph.getPasses() × pass.queue.getItems()` to sync every item's vertex / index / descriptor resources and initialize `objectInfo` push-constants.
  - Call `resourceManager->preloadPipelines(m_frameGraph.collectAllPipelineBuildInfos())`.
  - SHALL NOT side-channel-inject any camera or light UBO into `RenderingItem::descriptorResources`. Scene-level UBOs flow through `Scene::getSceneLevelResources(pass, target)` merged inside `RenderQueue::buildFromScene`.
- `uploadData()`: Iterate `m_frameGraph.getPasses() × pass.queue.getItems()` and `syncResource` every item's vertex buffer, index buffer, and descriptor resources.
- `draw()`: Acquire image, record commands, iterate `m_frameGraph.getPasses() × pass.queue.getItems()` binding the pipeline/resources and calling `cmd->drawItem(item)` for each, submit, present.
- Resolving the bound graphics pipeline using `RenderingItem::pipelineKey` and the resource manager pipeline cache on each draw.

The `VulkanRenderer::Impl` class SHALL hold the `FrameGraph` as a member whose lifetime matches the scene binding. The `Impl` class SHALL NOT hold a cached single `RenderingItem` member.

#### Scenario: Triangle rendering loop
- **WHEN** Renderer has initialized with a scene containing one `RenderableSubMesh` and one `Camera` whose target is `nullopt`
- **THEN** `initScene` derives the swapchain target, backfills the camera's target to it, builds the frame graph with one pass whose target equals the backfilled camera target, and `draw()` iterates the one pass × one item path calling `acquireNextImage` / `beginCommandBuffer` / `beginRenderPass` / `bindPipeline` / `bindResources` / `drawItem` / `endRenderPass` / `endCommandBuffer` / `queueSubmit` / `present`

#### Scenario: Backfill happens before buildFromScene
- **WHEN** a scene has a camera with `getTarget().has_value() == false` and `initScene(scene)` is called
- **THEN** by the time `m_frameGraph.buildFromScene(*scene)` runs, the camera's target equals the swapchain target, so `getSceneLevelResources` includes the camera's UBO for the forward pass

#### Scenario: Multi-renderable scene produces multi-item queue
- **WHEN** a scene has two `RenderableSubMesh` instances both supporting `Pass_Forward`, and `initScene(scene)` is called followed by `draw()`
- **THEN** the `draw()` loop iterates the one pass's queue and calls `cmd->drawItem(item)` exactly twice in PipelineKey-sorted order

#### Scenario: No side-channel UBO injection
- **WHEN** `initScene(scene)` is called on a scene with one camera (nullopt target, backfilled by initScene) and one directional light whose pass mask includes Forward
- **THEN** each resulting `RenderingItem` in `m_frameGraph.getPasses()[0].queue` carries the camera UBO and the light UBO in its `descriptorResources`, produced entirely by `RenderQueue::buildFromScene` via `Scene::getSceneLevelResources(Pass_Forward, swapchainTarget)`. No code inside `VulkanRenderer::Impl::initScene` manually pushes UBOs into any item.
