## Why

The `VulkanResourceManager` class has getter functions (lines 53-57 of `vk_resource_manager.hpp`) that return raw pointers (`T*`) for non-owning object references. According to the project's C++ style guide (`openspec/specs/cpp-style-guide/spec.md`), raw pointers for non-owning references must be replaced with references (`T&`). This violation creates ambiguity about ownership semantics and risks dangling pointer bugs.

## What Changes

- Change `VulkanBuffer* getBuffer(void* handle)` to `VulkanBuffer& getBuffer(void* handle)`
- Change `VulkanTexture* getTexture(void* handle)` to `VulkanTexture& getTexture(void* handle)`
- Change `VulkanShader* getShader(void* handle)` to `VulkanShader& getShader(void* handle)`
- Change `VulkanRenderPass* getRenderPass()` to `VulkanRenderPass& getRenderPass()`
- Change `VulkanPipelineBase* getRenderPipeline()` to `VulkanPipelineBase& getRenderPipeline()`
- Update all call sites that use these functions to work with references instead of pointers

## Capabilities

### New Capabilities

None - this is a refactoring task with no new capabilities.

### Modified Capabilities

None - this is an implementation-level refactor that does not change spec-level behavior.

## Impact

- **Affected Files**:
  - `src/backend/vulkan/details/vk_resource_manager.hpp` (interface definition)
  - `src/backend/vulkan/details/vk_resource_manager.cpp` (implementation)
  - All files that call these getter functions (likely in tests and other Vulkan backend components)
- **API Change**: Return types change from pointer to reference - callers must update to use `.` instead of `->`
- **No Behavioral Change**: The functions return valid objects or throw/reference still cannot be null in this context
