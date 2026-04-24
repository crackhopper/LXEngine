## 1. VulkanDevice Implementation (Foundation)

- [x] 1.1 Implement `VulkanDevice::initialize()` - Create Vulkan instance with required extensions
- [x] 1.2 Implement `VulkanDevice::pickPhysicalDevice()` - Enumerate and select discrete GPU or first available
- [x] 1.3 Implement `VulkanDevice::createLogicalDevice()` - Create device with graphics and present queues
- [x] 1.4 Implement `VulkanDevice::findMemoryTypeIndex()` - Memory type selection helper
- [x] 1.5 Implement `VulkanDevice::shutdown()` - Cleanup device and instance
- [x] 1.6 Create `test_vulkan_device.cpp` - Test device initialization and queue creation
- [x] 1.7 Verify device test passes

## 2. Resource Creation - Buffers

- [x] 2.1 Implement `VulkanBuffer` constructor - Create buffer with specified usage and size
- [x] 2.2 Implement `VulkanBuffer::map()` - Map device memory for host access
- [x] 2.3 Implement `VulkanBuffer::unmap()` - Unmap device memory
- [x] 2.4 Implement `VulkanBuffer::uploadData()` - Staging buffer copy to device local
- [x] 2.5 Implement `VulkanBuffer::copyTo()` - Buffer-to-buffer copy
- [x] 2.6 Create `test_vulkan_buffer.cpp` - Test vertex and index buffer creation and upload
- [x] 2.7 Verify buffer test passes

## 3. Resource Creation - Textures

- [x] 3.1 Implement `VulkanTexture` constructor - Create image and allocate memory
- [x] 3.2 Implement `VulkanTexture::createImageView()` - Create image view for shader access
- [x] 3.3 Implement `VulkanTexture::transitionLayout()` - Image layout transition via pipeline barrier
- [x] 3.4 Implement `VulkanTexture::copyFromBuffer()` - Copy from staging buffer to device local
- [x] 3.5 Create `test_vulkan_texture.cpp` - Test texture creation and layout transitions
- [x] 3.6 Verify texture test passes

## 4. Resource Creation - Shaders

- [x] 4.1 Implement `VulkanShader` constructor - Load SPIR-V bytecode from filesystem
- [x] 4.2 Implement `VulkanShader::getStageCreateInfo()` - Return VkPipelineShaderStageCreateInfo for pipeline
- [x] 4.3 Use shader path convention: `assets/shaders/glsl/{shaderName}.vert.spv` and `assets/shaders/glsl/{shaderName}.frag.spv`
- [x] 4.4 Verify `assets/shaders/glsl/blinnphong_0.vert.spv` and `blinnphong_0.frag.spv` exist (built by CompileShaders target)
- [x] 4.5 Create `test_vulkan_shader.cpp` - Test shader module loading
- [x] 4.6 Verify shader test passes

## 5. Render Pass & Framebuffer

- [x] 5.1 Implement `VulkanRenderPass` constructor - Create render pass with color and depth attachments
- [x] 5.2 Implement `VulkanRenderPass::setClearColor()` - Configure clear values
- [x] 5.3 Implement `VulkanFrameBuffer` constructor - Create framebuffer from attachments
- [x] 5.4 Create `test_vulkan_renderpass.cpp` - Test render pass creation
- [x] 5.5 Create `test_vulkan_framebuffer.cpp` - Test framebuffer creation
- [x] 5.6 Verify renderpass and framebuffer tests pass

## 6. Swapchain Depth Resources

- [x] 6.1 Implement `VulkanSwapchain::createDepthResources()` - Create depth image with memory and view
- [x] 6.2 Verify depth resources are properly created before render pass begins
- [x] 6.3 Create `test_vulkan_depth.cpp` - Test depth resource creation
- [x] 6.4 Verify depth test passes

## 7. Command Buffer Management

- [x] 7.1 Implement `VulkanCommandBufferManager` constructor and pool creation
- [x] 7.2 Implement `VulkanCommandBufferManager::beginSingleTimeCommands()` - Allocate and begin single-use buffer
- [x] 7.3 Implement `VulkanCommandBufferManager::endSingleTimeCommands()` - End and execute single-use buffer
- [x] 7.4 Implement `VulkanCommandBuffer::beginRenderPass()` - Begin render pass with framebuffer
- [x] 7.5 Implement `VulkanCommandBuffer::setViewport()` - Set dynamic viewport
- [x] 7.6 Implement `VulkanCommandBuffer::setScissor()` - Set dynamic scissor
- [x] 7.7 Implement `VulkanCommandBuffer::bindPipeline()` - Bind graphics pipeline
- [x] 7.8 Implement `VulkanCommandBuffer::bindResources()` - Bind descriptor sets
- [x] 7.9 Implement `VulkanCommandBuffer::drawItem()` - Draw indexed primitives with push constants
- [x] 7.10 Create `test_vulkan_command_buffer.cpp` - Test command recording
- [x] 7.11 Verify command buffer test passes

## 8. Pipeline Construction

- [x] 8.1 Implement `VulkanPipelineBase` shader loading and pipeline layout creation
- [x] 8.2 Implement `VulkanPipelineBase::getVertexInputStateCreateInfo()` - Convert vertex format to input state
- [x] 8.3 Implement `VulkanPipelineBase::getViewportStateCreateInfo()` - Configure viewport and scissor
- [x] 8.4 Implement `VulkanPipelineBase::getRasterizationStateCreateInfo()` - Configure rasterizer
- [x] 8.5 Implement `VulkanPipelineBase::getColorBlendStateCreateInfo()` - Configure color blending
- [x] 8.6 Implement `VkPipelineBlinnPhong` in `vkp_blinnphong.cpp` - Blinn-Phong pipeline specialization
- [x] 8.7 Create `test_vulkan_pipeline.cpp` - Test pipeline creation
- [x] 8.8 Verify pipeline test passes

## 9. Resource Manager Integration

- [x] 9.1 Implement `VulkanResourceManager::initializeRenderPassAndPipeline()` - Create render pass and pipelines
- [x] 9.2 Implement resource type mapping (IRenderResource type to Vulkan object)
- [x] 9.3 Create `test_vulkan_resource_manager.cpp` - Test resource management
- [x] 9.4 Verify resource manager test passes

## 10. Renderer Lifecycle Implementation

- [x] 10.1 Implement `VulkanRenderer::shutdown()` - Destroy all Vulkan resources in reverse creation order
- [x] 10.2 Implement `VulkanRenderer::initScene()` - Create GPU resources from Scene/RenderingItem
- [x] 10.3 Implement `VulkanRenderer::uploadData()` - Upload dirty resources per frame
- [x] 10.4 Implement `VulkanRenderer::draw()` - Full render loop (acquire, record, submit, present)
- [x] 10.5 Create `test_vulkan_renderer.cpp` - Test full triangle render
- [x] 10.6 Verify triangle renders correctly

## 11. Shader Build Verification

- [x] 11.1 Verify `shaders/CMakeLists.txt` works with glslc
- [x] 11.2 Verify `CompileShaders` target produces correct .spv files
- [x] 11.3 Document shader path conventions if different from expected

## 12. Compilation Fixes (Pre-requisite)

- [x] 12.1 Fix include path in `vkp_blinnphong.hpp`: `core/resources/vertex.hpp` -> `core/resources/vertex_buffer.hpp`
- [x] 12.2 Fix include path in `vkp_pipeline.hpp`: `core/resources/vertex.hpp` -> `core/resources/vertex_buffer.hpp`
- [x] 12.3 Verify all items compile after fixes
- [x] 12.4 Fix `vertex_buffer.hpp` missing includes (`core/gpu/render_resource.hpp`, `as_tuple` -> `this->as_tuple`)
- [x] 12.5 Fix `index_buffer.hpp` missing includes and `u32` -> `uint32_t`
- [x] 12.6 Fix `mesh.hpp` include path and `create()` return type
- [x] 12.7 Fix `object.hpp` forward declaration ordering and `create()` return type
- [x] 12.8 Fix `scene.hpp` `Camera`/`DirectionalLight` construction and `create()` return type
- [x] 12.9 Fix `vk_device.hpp/cpp` nested namespace syntax (MSVC compatibility)
- [x] 12.10 Fix `vkd_descriptor_manager.hpp/cpp` nested namespace syntax and `DescriptorUpdateInfo` ordering
- [x] 12.11 LX_Core builds successfully
- [x] 12.12 Fix circular dependencies using forward declarations (VulkanRenderPass, VulkanPipeline)
- [x] 12.13 Fix `VulkanResourceManager` using raw pointers for incomplete types
- [x] 12.14 Fix `VulkanRenderContext` duplicate definition (removed from vkc_cmdbuffer.hpp)
- [x] 12.15 **LX_GraphicsBackend builds successfully**
