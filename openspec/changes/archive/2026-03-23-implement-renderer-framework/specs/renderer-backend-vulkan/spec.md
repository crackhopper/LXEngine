## Global Information

### Build System
- **Main CMakeLists.txt**: `renderer/CMakeLists.txt` - project entry point
- **Shader CMakeLists.txt**: `renderer/shaders/CMakeLists.txt` - compiles GLSL to SPIR-V (.spv)
- **Shader naming**: Code expects shader name `blinnphong_0` → `shaders/glsl/blinnphong_0.vert.spv` / `blinnphong_0.frag.spv`
- **Build command**: `cmake --build . --target CompileShaders` to build shaders

### Directory Structure
- `renderer/shaders/glsl/` - GLSL shader source files
- `renderer/shaders/` - CMakeLists.txt for shader compilation
- `renderer/src/backend/vulkan/details/` - Vulkan backend implementation
- `renderer/src/test/` - Integration tests

---

## ADDED Requirements

### Requirement: VulkanDevice shall initialize Vulkan instance and enumerate physical devices

The VulkanDevice SHALL initialize the Vulkan subsystem with:
- VK_INSTANCE with required global extensions (Vulkan surface for window integration)
- Enumerate physical devices and select the first discrete GPU if available, otherwise the first available device
- Log device properties (name, type, driver version) for debugging

#### Scenario: Device initialization succeeds with valid GPU
- **WHEN** VulkanDriver is installed and system has a discrete GPU
- **THEN** VulkanDevice::initialize() SHALL return true and m_physicalDevice SHALL be valid

#### Scenario: Device initialization fails gracefully on no GPU
- **WHEN** System has no Vulkan-capable GPU
- **THEN** VulkanDevice::initialize() SHALL return false and log appropriate error message

### Requirement: VulkanDevice shall create logical device with graphics and present queues

The VulkanDevice SHALL create a logical device with:
- Graphics queue family supporting rendering commands
- Present queue family supporting window presentation
- Device extensions required for swapchain (VK_KHR_SWAPCHAIN_EXTENSION_NAME)
- Memory allocator initialized for efficient GPU memory management

#### Scenario: Logical device creation with graphics queue
- **WHEN** Physical device supports graphics operations
- **THEN** VulkanDevice SHALL create m_device and m_graphicsQueue with at least one queue

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
- Binding descriptor sets for all pipeline slots
- Drawing indexed primitives with push constants

#### Scenario: Recording triangle draw commands
- **WHEN** Recording commands for triangle rendering
- **THEN** Command buffer SHALL have: beginRenderPass, setViewport, setScissor, bindPipeline, bindVertexBuffer, bindIndexBuffer, drawIndexed

### Requirement: VulkanPipeline shall create graphics pipelines with shader stages

The VulkanPipeline SHALL support:
- Creating pipeline layout with descriptor set layouts and push constant ranges
- Loading vertex and fragment shader modules
- Defining vertex input state from vertex format
- Configuring rasterization, input assembly, viewport, depth stencil, color blending
- Creating complete graphics pipeline

#### Scenario: Blinn-Phong pipeline creation
- **WHEN** Creating Blinn-Phong pipeline with vertex buffer containing position/normal/uv
- **THEN** Pipeline SHALL be created with shader stages, vertex input state, rasterization, and all fixed-function state configured

### Requirement: VulkanRenderer shall implement complete render lifecycle

The VulkanRenderer SHALL implement:
- initialize(WindowPtr): Create device, swapchain, command buffers, and resources
- shutdown(): Destroy all Vulkan objects in reverse creation order
- initScene(ScenePtr): Create GPU resources from scene's RenderingItem
- uploadData(): Upload dirty resources to GPU
- draw(): Acquire image, record commands, submit, present

#### Scenario: Triangle rendering loop
- **WHEN** Renderer has initialized with triangle scene
- **THEN** draw() SHALL call: acquireNextImage, beginCommandBuffer, beginRenderPass, bindPipeline, bindVertexBuffer, bindIndexBuffer, drawIndexed, endRenderPass, endCommandBuffer, queueSubmit, present

### Requirement: VulkanResourceManager shall map IRenderResource to Vulkan objects

The VulkanResourceManager SHALL support:
- Creating VulkanBuffer from IRenderResource with type VertexBuffer or IndexBuffer
- Creating VulkanTexture from IRenderResource with type CombinedImageSampler
- Maintaining map of IRenderResource* to created Vulkan objects
- Initializing render pass and pipeline with correct formats

#### Scenario: Resource mapping for vertex buffer
- **WHEN** initScene creates GPU resource for vertex buffer IRenderResource
- **THEN** VulkanResourceManager SHALL store mapping and return valid VulkanBuffer

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
