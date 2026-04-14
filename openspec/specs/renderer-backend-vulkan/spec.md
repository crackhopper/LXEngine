# renderer-backend-vulkan Specification

## Purpose
TBD - created by archiving change implement-renderer-framework. Update Purpose after archive.
## Requirements
### Requirement: VulkanDevice shall initialize Vulkan instance and enumerate physical devices

The VulkanDevice SHALL initialize the Vulkan subsystem with:
- VK_INSTANCE with required global extensions (Vulkan surface for window integration)
- Enumerate physical devices and select the first discrete GPU if available, otherwise the first available device
- Log device properties (name, type, driver version) for debugging
- Factory pattern: VulkanDevice objects MUST be created via `VulkanDevice::create()` with Token
- Initialization requires `WindowPtr` and application name parameters

#### Scenario: Device initialization succeeds with valid GPU
- **WHEN** VulkanDriver is installed and system has a discrete GPU
- **WHEN** `VulkanDevice::create()` is called and `initialize(window, "AppName")` is invoked
- **THEN** `m_physicalDevice` SHALL be valid and Vulkan instance created successfully
- **THEN** Graphics and present queues SHALL be available
- **THEN** `getInstance()` SHALL return valid VkInstance

#### Scenario: Device initialization fails gracefully on no GPU
- **WHEN** System has no Vulkan-capable GPU
- **THEN** `initialize()` SHALL throw `std::runtime_error` with appropriate error message
- **AND** No Vulkan resources SHALL be leaked

### Requirement: VulkanDevice shall create logical device with graphics and present queues

The VulkanDevice SHALL create a logical device with:
- Graphics queue family supporting rendering commands
- Present queue family supporting window presentation
- Device extensions required for swapchain (VK_KHR_SWAPCHAIN_EXTENSION_NAME)
- VulkanDescriptorManager initialized for efficient GPU resource management

#### Scenario: Logical device creation with graphics queue
- **WHEN** Physical device supports graphics operations
- **WHEN** `initialize(window, "AppName")` is invoked
- **THEN** `m_device` SHALL be valid VkDevice
- **AND** `m_graphicsQueue` SHALL be valid with at least one queue
- **AND** `getDescriptorManager()` SHALL return reference to initialized manager

### Requirement: VulkanBuffer shall create and manage GPU-accessible memory

The VulkanBuffer SHALL support:
- Creating buffers with VK_BUFFER_USAGE_VERTEX_BUFFER_BIT or VK_BUFFER_USAGE_INDEX_BUFFER_BIT
- Allocating device-local memory with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
- Staging buffer creation for data transfer from host memory
- Memory copy from staging buffer to device-local buffer
- Mapping host memory for direct access when needed

#### Scenario: Vertex buffer creation and data upload
- **WHEN** Creating a vertex buffer with 3 vertices (each 32 bytes)
- **THEN** Buffer SHALL be created with size 96 bytes, staging buffer SHALL copy data to device-local buffer

#### Scenario: Index buffer creation and data upload
- **WHEN** Creating an index buffer with 3 indices (each 4 bytes)
- **THEN** Buffer SHALL be created with size 12 bytes, staging buffer SHALL copy data to device-local buffer

### Requirement: VulkanTexture shall create and manage GPU texture resources

The VulkanTexture SHALL support:
- Creating 2D textures with specified width, height, and format
- Allocating device-local memory for texture data
- Staging buffer for pixel data transfer
- Image layout transitions using pipeline barriers
- Creating image views for shader access

#### Scenario: Texture creation from pixel data
- **WHEN** Creating a 2D texture with 256x256 RGBA pixels
- **THEN** Texture SHALL be created with VkFormat VK_FORMAT_R8G8B8A8_UNORM and image layout transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

### Requirement: VulkanShader shall load SPIR-V shader modules

The VulkanShader SHALL support:
- Loading compiled SPIR-V bytecode from filesystem
- Creating VkShaderModule for both vertex and fragment shaders
- Providing VkPipelineShaderStageCreateInfo for pipeline construction
- Caching loaded shader modules to avoid redundant file reads
- Loading from path pattern: `shaders/glsl/{shaderName}.vert.spv` and `shaders/glsl/{shaderName}.frag.spv`

#### Scenario: Vertex shader module creation
- **WHEN** Loading vertex shader for "blinnphong_0" shader
- **THEN** VulkanShader SHALL load `shaders/glsl/blinnphong_0.vert.spv` and create valid VkShaderModule with VK_SHADER_STAGE_VERTEX_BIT

#### Scenario: Fragment shader module creation
- **WHEN** Loading fragment shader for "blinnphong_0" shader
- **THEN** VulkanShader SHALL load `shaders/glsl/blinnphong_0.frag.spv` and create valid VkShaderModule with VK_SHADER_STAGE_FRAGMENT_BIT

#### Scenario: Shader files built by CMake
- **WHEN** Building the project with CMake
- **THEN** `CompileShaders` target SHALL produce `.spv` files from `.vert`/`.frag` sources using glslc

### Requirement: VulkanRenderPass shall define render pass with color and depth attachments

The VulkanRenderPass SHALL support:
- Color attachment with specified format (from swapchain)
- Depth attachment with specified format (VK_FORMAT_D32_SFLOAT or similar)
- Clear values for color (0,0,0,1) and depth (1.0)
- Beginning render pass with VK_SUBPASS_CONTENTS_INLINE

#### Scenario: Render pass creation with standard attachments
- **WHEN** Creating render pass with color=D32F and depth=VK_FORMAT_B8G8R8A8_UNORM
- **THEN** Render pass SHALL be created and m_renderPass SHALL be valid

### Requirement: VulkanFrameBuffer shall bind render pass to actual images

The VulkanFrameBuffer SHALL support:
- Creating framebuffer from render pass, swapchain images, and depth image
- Binding color and depth image views to framebuffer
- Returning framebuffer dimensions for viewport/scissor setup

#### Scenario: Framebuffer creation from swapchain
- **WHEN** Creating framebuffer for swapchain image with dimensions 800x600
- **THEN** Framebuffer SHALL be created with width=800, height=600 and all attachments bound

### Requirement: VulkanSwapchain shall manage window-presentation synchronization

The VulkanSwapchain SHALL support:
- Querying surface capabilities and selecting appropriate image count
- Creating swapchain with VK_PRESENT_MODE_FIFO_KHR (vsync)
- Acquiring next image with semaphore for render synchronization
- Presenting rendered image to window

#### Scenario: Swapchain initialization
- **WHEN** Creating swapchain for window 800x600
- **THEN** Swapchain SHALL have minImageCount >= 2 and extent 800x600

#### Scenario: Depth resource creation
- **WHEN** Creating depth resources for swapchain
- **THEN** Depth image SHALL be created with VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, and VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT

### Requirement: VulkanCommandBufferManager shall manage command buffer lifecycle

The VulkanCommandBufferManager SHALL support:
- Creating command pool for graphics queue family
- Allocating command buffers for rendering
- Beginning command buffer for single-time commands
- Ending and executing single-time commands (transfers)
- Resetting command pools for reuse

#### Scenario: Single-time command execution
- **WHEN** Executing single-time command to copy buffer data
- **THEN** Command buffer SHALL be allocated, recorded, submitted, and freed

### Requirement: VulkanCommandBuffer shall record rendering commands

The VulkanCommandBuffer SHALL support:
- Beginning render pass with specified framebuffer and render pass
- Setting viewport to framebuffer dimensions
- Setting scissor to framebuffer dimensions
- Binding graphics pipeline
- Binding vertex and index buffers
- Binding descriptor sets by matching each descriptor binding from the pipeline's reflected `ShaderResourceBinding` list against the `RenderingItem::descriptorResources` via `IRenderResource::getBindingName()`. The matching SHALL NOT use `PipelineSlotId`; that enum SHALL not exist.
- Drawing indexed primitives with push constants

#### Scenario: Recording triangle draw commands
- **WHEN** Recording commands for triangle rendering
- **THEN** Command buffer SHALL have: beginRenderPass, setViewport, setScissor, bindPipeline, bindVertexBuffer, bindIndexBuffer, drawIndexed

#### Scenario: Recording a draw binds descriptors by name
- **WHEN** Recording commands for a `RenderingItem` whose pipeline exposes a `"CameraUBO"` binding at set 1 binding 0 and whose `descriptorResources` contains a resource whose `getBindingName()` returns `StringID("CameraUBO")`
- **THEN** The command buffer updates the descriptor set at `(set=1, binding=0)` to reference that resource's GPU buffer, with no lookup table from a hardcoded enum

### Requirement: VulkanPipeline shall create graphics pipelines with shader stages

The VulkanPipeline SHALL consume a `LX_core::PipelineBuildInfo` (from the `pipeline-build-info` capability) as its single construction input, and SHALL support:

- Creating `VkShaderModule`s from `buildInfo.stages` (bytecode only; no filesystem loads at this layer)
- Building `VkDescriptorSetLayout`s from `buildInfo.bindings` (reflected `ShaderResourceBinding` list) — one layout per distinct `set` number, each layout populated from the `(binding, type, stageFlags)` of every `ShaderResourceBinding` whose `set` matches
- Creating a pipeline layout with the derived descriptor set layouts and the `buildInfo.pushConstant` range
- Defining vertex input state from `buildInfo.vertexLayout`
- Configuring rasterization, input assembly, viewport, depth stencil, color blending from `buildInfo.renderState` and `buildInfo.topology`
- Creating the final `VkPipeline` via `vkCreateGraphicsPipelines`

The pipeline class SHALL NOT accept any legacy `PipelineSlotDetails` vector and SHALL NOT key descriptor layout construction on hardcoded slot enums.

#### Scenario: Dynamic graphics pipeline creation from PipelineBuildInfo

- **WHEN** A caller invokes pipeline creation with a `PipelineBuildInfo` whose `bindings` contains a UBO at set 0 binding 0, a sampler at set 2 binding 1, and whose `vertexLayout` includes position+normal+uv
- **THEN** The constructed `VulkanPipeline` exposes exactly the descriptor set layouts and vertex input attributes described by the build info, with no reference to any shader-name lookup table

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

### Requirement: Integration tests shall verify each module independently

The test suite SHALL include:
- test_vulkan_device: Test device initialization and queue creation
- test_vulkan_buffer: Test buffer creation and data upload
- test_vulkan_texture: Test texture creation and layout transitions
- test_vulkan_shader: Test shader module loading
- test_vulkan_renderpass: Test render pass creation
- test_vulkan_framebuffer: Test framebuffer creation
- test_vulkan_swapchain: Test swapchain creation and depth resources
- test_vulkan_command_buffer: Test command recording
- test_vulkan_pipeline: Test pipeline creation
- test_vulkan_renderer: Test full render loop with triangle

#### Scenario: Device test passes
- **WHEN** test_vulkan_device runs successfully
- **THEN** All assertions SHALL pass and Vulkan resources are valid

#### Scenario: Triangle render test passes
- **WHEN** test_vulkan_renderer renders a triangle
- **THEN** Window SHALL display triangle without validation errors or crashes

## REMOVED Requirements

### Requirement: VulkanDevice::initialize() shall return boolean success indicator

**Reason**: Implementation changed to exception-based error handling. Boolean returns were removed in favor of `std::runtime_error` exceptions.

**Migration**: Wrap initialization in try/catch block. Catch `std::exception` for error handling.

### Requirement: VulkanDevice shall have public constructor

**Reason**: Factory pattern enforcement via private Token struct.

**Migration**: Use `VulkanDevice::create()` factory method instead of direct construction.

