## 1. Implementation Boundary

- [x] 1.1 Remove unnecessary duplicate renderer inheritance from `VulkanRendererImpl`.
- [x] 1.2 Make implementation members private and route access through explicit internal methods.

## 2. Internal Cleanup

- [x] 2.1 Consolidate duplicated renderer constants such as frames-in-flight.
- [x] 2.2 Re-evaluate callback placement and keep the smallest sensible public surface.

## 3. Verification

- [x] 3.1 Build and run focused Vulkan renderer tests after the refactor.
- [x] 3.2 Confirm no external code still depends on direct implementation member access.
