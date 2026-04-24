## Context

The VulkanDevice class was refactored to:
1. Enforce factory pattern via private `Token` struct
2. Change `initialize()` to require `WindowSharedPtr` and `appName` parameters
3. Manage `VulkanDescriptorManager` as a member (initialized after logical device creation)

The tests currently call `device->initialize()` with no arguments, which is now invalid. The spec document also describes the old boolean-return interface.

## Goals / Non-Goals

**Goals:**
- Fix test compilation by providing required parameters to `initialize()`
- Update spec to reflect new interface

**Non-Goals:**
- Adding new VulkanDevice functionality
- Creating test infrastructure for window management (tests use exception-based skip pattern)

## Decisions

1. **Test initialization approach**: Tests will create a minimal test window using the existing `Window` abstraction, or use the skip pattern for environments without Vulkan support

2. **Spec update scope**: Only update the `renderer-backend-vulkan/spec.md` section on VulkanDevice initialization to reflect:
   - Factory pattern requirement
   - New `initialize(WindowSharedPtr, appName, ...)` signature
   - Exception-based error handling

## Risks / Trade-offs

- [Risk] Tests may fail on machines without Vulkan/GPU → [Mitigation] Tests already use try/catch with skip pattern
- [Risk] Test window creation requires platform code → [Mitigation] Use existing `infra/window/window.hpp` infrastructure

## Migration Plan

1. Update `openspec/specs/renderer-backend-vulkan/spec.md` with new VulkanDevice interface
2. Update each test file to use `device->initialize(window, "TestApp")` pattern
3. Verify compilation
