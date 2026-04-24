## Why

`VulkanResourceManager` currently keys GPU resource cache entries by a raw `this` pointer returned from CPU resources. That creates address-reuse risk and encourages frame-to-frame cache churn that is coupled to the active-set garbage-collection path.

This change replaces incidental object-address identity with a stable resource identity contract.

## What Changes

- Define a stable identity contract for CPU-side GPU resources.
- Stop using raw object addresses as the sole cache key.
- Clarify garbage-collection expectations around temporarily unused resources.

## Capabilities

### New Capabilities
- `gpu-resource-identity`: Stable identity contract for CPU resources synchronized into backend GPU resource caches.

### Modified Capabilities

## Impact

- Affected code: `src/backend/vulkan/details/resource_manager.cpp`, core resource interfaces
- Affected APIs: resource-handle semantics
- Affected systems: cache correctness, lifetime stability, future resource reuse
