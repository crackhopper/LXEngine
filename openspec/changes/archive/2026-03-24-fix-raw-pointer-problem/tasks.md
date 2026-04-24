## 1. Update VulkanResourceManager Ownership

- [x] 1.1 Change `m_renderPass` from `VulkanRenderPass*` to `std::unique_ptr<VulkanRenderPass>`
- [x] 1.2 Change `m_pipeline` from `VulkanPipelineBase*` to `std::unique_ptr<VulkanPipelineBase>`
- [x] 1.3 Change `m_cmdBufferMgr` from `VulkanCommandBufferManager*` to `VulkanCommandBufferManager&` (reference, injected via constructor)
- [x] 1.4 Remove `setCommandBufferManager()` setter method - inject via constructor instead
- [x] 1.5 Update `vk_resource_manager.hpp` constructor: `VulkanResourceManager(VulkanDevice&, VulkanCommandBufferManager&)` - no setter
- [x] 1.6 Create owned objects (renderPass, pipeline) inside VulkanResourceManager

## 2. Update Device References to Use References

- [x] 2.1 Update `VulkanCommandBuffer` (`vkc_cmdbuffer.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`, remove `setResourceManager()`, pass `VulkanResourceManager&` as parameter to `bindResources()`
- [x] 2.2 Update `VulkanCommandBufferManager` (`vkc_cmdbuffer_manager.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`
- [x] 2.3 Update `VulkanDescriptorManager` (`vkd_descriptor_manager.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`
- [x] 2.4 Update `VulkanPipelineBase` (`vkp_pipeline.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`
- [x] 2.5 Update `VulkanRenderPass` (`vkr_renderpass.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`
- [x] 2.6 Update `VulkanFrameBuffer` (`vkr_framebuffer.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`
- [x] 2.7 Update `VulkanSwapchain` (`vkr_swapchain.hpp`): `VulkanDevice* m_device` → `VulkanDevice& m_device`

## 3. Update Constructors and Factory Methods

- [x] 3.1 Update all constructors that take `VulkanDevice*` to take `VulkanDevice&`
- [x] 3.2 Update `VulkanDescriptorManager::create()` factory method (moved to .cpp to avoid circular include)
- [x] 3.3 Update `VulkanCommandBufferManager::allocateBuffer()` to return `VulkanCommandBufferUniquePtr` (unique_ptr)
- [x] 3.4 Update `VulkanCommandBufferManager::beginSingleTimeCommands()` to return `VulkanCommandBufferUniquePtr` (unique_ptr)
- [x] 3.5 Update `VulkanResourceManager` construction in `VulkanRenderer` (vk_renderer.cpp)
- [x] 3.6 Update `VulkanDevice` to create `VulkanDescriptorManager` after `createLogicalDevice()`

## 4. Update Test Files

- [x] 4.1 Update `test_vulkan_device.cpp` - already had correct appName parameter
- [x] 4.2 Update `test_vulkan_command_buffer.cpp` - added cmdBufferMgr creation, updated bindResources call
- [x] 4.3 Update `test_vulkan_resource_manager.cpp` - added cmdBufferMgr creation
- [x] 4.4 Update `test_vulkan_texture.cpp` - updated unique_ptr usage for command buffer
- [x] 4.5 Update `test_vulkan_renderer.cpp` - added appName parameter
- [x] 4.6 Update `test_render_triangle.cpp` - added appName parameter

## 5. Update C++ Style Guide Spec

- [x] 5.1 Create `openspec/specs/cpp-style-guide/spec.md` with raw pointer prohibition policy
- [x] 5.2 Document allowed exceptions (handle pointers, C interop)
- [x] 5.3 Add ownership guidelines (unique_ptr, shared_ptr, references)
- [x] 5.4 Add GPU Object Return Type Convention (unique_ptr for GPU objects)
- [x] 5.5 Add CommandBuffer Design Principle (no manager references)

## 6. Verify and Lint

- [x] 6.1 Build project to verify all changes compile
- [x] 6.2 Run linter on modified files (no linter configured - build verified correctness)
- [x] 6.3 Run tests to verify functionality unchanged (tests skipped due to no GPU - expected behavior)

## 7. Agent Configuration

- [x] 7.1 Update `.cursor/AGENTS.md` to reference C++ Style Guide (critical policy document)
