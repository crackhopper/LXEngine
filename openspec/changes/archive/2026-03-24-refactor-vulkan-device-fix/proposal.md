## Why

The VulkanDevice class interface was refactored to use a factory pattern with Token for controlled construction and a new `initialize()` signature that accepts a `WindowSharedPtr` and application name. The existing tests and spec documentation are out of sync with these changes, causing compilation failures.

## What Changes

- **BREAKING**: `VulkanDevice::initialize()` now requires `WindowSharedPtr` and `appName` as mandatory parameters (previously took no arguments)
- Factory pattern enforced via private `Token` struct - objects must be created via `VulkanDevice::create()`
- `VulkanDescriptorManager` lifecycle now managed by `VulkanDevice` (stored as member, initialized after logical device creation)
- Updated queue family index accessors to use `std::optional::value_or()` pattern
- Error handling via exceptions instead of boolean return values

## Capabilities

### New Capabilities
- No new capabilities introduced

### Modified Capabilities
- `renderer-backend-vulkan`: VulkanDevice interface changes - initialization signature, factory pattern enforcement, exception-based error handling

## Impact

- All test files using `device->initialize()` need updating to pass required `WindowSharedPtr` and `appName`
- `openspec/specs/renderer-backend-vulkan/spec.md` documents outdated interface that needs updating
- `vk_renderer.cpp` already uses correct new signature
