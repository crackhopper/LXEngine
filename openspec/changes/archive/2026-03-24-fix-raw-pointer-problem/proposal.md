## Why

The Vulkan backend uses raw pointers to store references to other classes, which violates the project's C++ design policy. Raw pointers for ownership semantics are error-prone and obscure lifetime requirements. We need to eliminate raw pointers used as references to other objects, using appropriate smart pointers or references instead.

## What Changes

- **Non-owning references to `VulkanDevice`**: Replace `T*` with `T&` reference (device outlives all dependents, non-nullable invariant)
- **Injected dependencies**: All dependencies MUST be injected via constructor and stored as references (`T&`). No setter methods for dependency injection.
- **VulkanResourceManager ownership**: Clarify ownership of `m_renderPass`, `m_pipeline`, `m_cmdBufferMgr` - they should be owned by this class via `std::unique_ptr`
- **DescriptorSet reference**: The existing `VulkanDescriptorManager&` reference is correct and requires no change
- **Spec update**: Add C++ smart pointer usage policy to `openspec/specs/cpp-style-guide/spec.md`

## Capabilities

### Modified Capabilities

- `renderer-backend-vulkan`: Refactor raw pointer members to use references or smart pointers
- `cpp-style-guide`: Add policy section on raw pointer prohibition

### New Capabilities

- None

## Impact

- All classes in `src/backend/vulkan/details/` that hold raw pointers to `VulkanDevice` or other Vulkan objects will be refactored
- Constructor signatures may change (references instead of pointers)
- `VulkanResourceManager` will take ownership of render pass, pipeline, and command buffer manager
