## MODIFIED Requirements

### Requirement: VulkanPipeline shall create graphics pipelines with shader stages

The VulkanPipeline SHALL support:
- Creating pipeline layout with descriptor set layouts and push constant ranges
- Loading vertex and fragment shader modules
- Defining vertex input state from vertex format
- Configuring rasterization, input assembly, viewport, depth stencil, color blending
- Creating complete graphics pipeline

#### Scenario: Dynamic graphics pipeline creation from draw state

- **WHEN** Creating a graphics pipeline for a shader and mesh whose vertex layout includes position, normal, and UV (or equivalent layout required by that shader)
- **THEN** Pipeline SHALL be created with shader stages, vertex input state, rasterization, and all fixed-function state configured for that combination

### Requirement: VulkanRenderer shall implement complete render lifecycle

The VulkanRenderer SHALL implement:
- initialize(WindowSharedPtr): Create device, swapchain, command buffers, and resources
- shutdown(): Destroy all Vulkan objects in reverse creation order
- initScene(SceneSharedPtr): Create GPU resources from scene's RenderingItem
- uploadData(): Upload dirty resources to GPU
- draw(): Acquire image, record commands, submit, present
- Resolving the bound graphics pipeline using `RenderingItem::pipelineKey` and the resource manager pipeline cache on each draw that uses a built `RenderingItem`

#### Scenario: Triangle rendering loop

- **WHEN** Renderer has initialized with triangle scene
- **THEN** draw() SHALL call: acquireNextImage, beginCommandBuffer, beginRenderPass, bindPipeline, bindVertexBuffer, bindIndexBuffer, drawIndexed, endRenderPass, endCommandBuffer, queueSubmit, present

### Requirement: VulkanResourceManager shall map IRenderResource to Vulkan objects

The VulkanResourceManager SHALL support:
- Creating VulkanBuffer from IRenderResource with type VertexBuffer or IndexBuffer
- Creating VulkanTexture from IRenderResource with type CombinedImageSampler
- Maintaining map of IRenderResource* to created Vulkan objects
- Initializing render pass with correct formats
- Maintaining a cache of graphics `VulkanPipeline` instances keyed by `LX_core::PipelineKey` (using `PipelineKey::Hash`), creating and storing a pipeline on cache miss from the current draw’s shader and layout data, with no requirement for a fixed built-in pipeline name such as `blinnphong_0`

#### Scenario: Resource mapping for vertex buffer

- **WHEN** initScene creates GPU resource for vertex buffer IRenderResource
- **THEN** VulkanResourceManager SHALL store mapping and return valid VulkanBuffer

#### Scenario: Pipeline lookup by PipelineKey

- **WHEN** draw logic requests the pipeline for a `PipelineKey` already present in the cache
- **THEN** VulkanResourceManager SHALL return the existing `VulkanPipeline` without creating a duplicate
