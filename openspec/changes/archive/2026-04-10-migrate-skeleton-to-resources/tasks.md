## 1. Pipeline hash API (resource-pipeline-hash)

- [x] 1.1 Add `getPipelineHash() const` to `Mesh` in `src/core/resources/mesh.hpp`, delegating to `getLayoutHash()`.
- [x] 1.2 Add `getPipelineHash() const` to `RenderState` and `ShaderProgramSet` in `src/core/resources/material.hpp` (and nested types if needed), delegating to existing `getHash()` implementations.
- [x] 1.3 Update any internal call sites that should use the unified name at the “pipeline identity” boundary (optional in this change if nothing consumes it yet).

## 2. Skeleton resource migration (skeleton-resource)

- [x] 2.1 Create `src/core/resources/skeleton.hpp` with `Bone`, `SkeletonUBO`, and `Skeleton` moved from `scene/components`; remove `IComponent` inheritance.
- [x] 2.2 Implement `SkeletonUboPtr getUBO() const`, `bool hasSkeleton() const` (returns true for instances used as skinning sources), and `size_t getPipelineHash() const` per design/spec.
- [x] 2.3 Move or recreate `skeleton.cpp` next to the new header if non-inline definitions are required; delete `src/core/scene/components/skeleton.hpp` and `skeleton.cpp`.
- [x] 2.4 Register new sources in CMake if the build lists `.cpp` files explicitly.

## 3. Remove IComponent from scene types

- [x] 3.1 Update `Camera` in `src/core/scene/camera.hpp`: drop `IComponent`, remove `getRenderResources()`, add `CameraUBOPtr getUBO() const` (or document public `ubo` if kept as the accessor).
- [x] 3.2 Update `DirectionalLight` in `src/core/scene/light.hpp` the same way for `DirectionalLightUboPtr`.
- [x] 3.3 Delete `src/core/scene/components/base.hpp` and remove the empty `components/` directory after all references are gone.

## 4. Includes and call sites

- [x] 4.1 Update `src/core/scene/object.hpp`: include `core/resources/skeleton.hpp` (and fix material/mesh includes if they still point at `scene/components/`); replace `skeleton.value()->getRenderResources()` with UBO-based descriptor list assembly (e.g. push `getUBO()` as `IRenderResourcePtr`).
- [x] 4.2 Remove `#include "core/scene/components/base.hpp"` from `src/core/resources/mesh.hpp` if it only existed for `IComponent`.
- [x] 4.3 Update `src/backend/vulkan/vk_renderer.cpp` to use camera/light `getUBO()` (or equivalent) instead of `getRenderResources()`.
- [x] 4.4 Update `src/backend/vulkan/details/pipelines/vkp_blinnphong.hpp` and any other files including the old skeleton path.
- [x] 4.5 Update `src/test/integration/test_vulkan_command_buffer.cpp` (and any other tests) to match new includes and UBO accessors.

## 5. Verification

- [x] 5.1 `grep` the tree for `IComponent`, `getRenderResources`, and `scene/components/skeleton` — expect no matches outside docs/history.
- [x] 5.2 Configure and build (`cmake .. -G Ninja && ninja BuildTest` or project-standard targets); fix compile/link errors.
- [x] 5.3 Run integration tests on available GPU or document skips per project norms.
