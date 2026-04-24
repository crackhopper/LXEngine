## Why

`VulkanRendererImpl` currently mixes implementation storage, public data exposure, and duplicated renderer inheritance in a way the review identifies as structurally awkward. The current shape works, but it makes ownership, API boundaries, and future renderer extension harder to reason about.

This change reduces `VulkanRendererImpl` to a cleaner internal implementation contract.

## What Changes

- Remove unnecessary duplicated renderer inheritance from `VulkanRendererImpl`.
- Make implementation members private and reduce direct external reach-through.
- Consolidate duplicated renderer constants/config such as frames-in-flight.
- Re-evaluate where renderer-only callbacks belong.

## Capabilities

### New Capabilities
- `vulkan-renderer-impl`: Internal design contract for Vulkan renderer implementation boundaries and encapsulation.

### Modified Capabilities

## Impact

- Affected code: `src/backend/vulkan/vulkan_renderer.*`
- Affected APIs: internal structure, possibly callback placement
- Affected systems: encapsulation, maintainability, renderer evolution
