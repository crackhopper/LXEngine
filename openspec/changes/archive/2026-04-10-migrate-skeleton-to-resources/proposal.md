## Why

Mesh and Material already live under `src/core/resources/`; Skeleton still sits in `src/core/scene/components/` behind a thin `IComponent` abstraction that only `Skeleton` implements. That split hides intent and adds no real polymorphism. Aligning Skeleton with other GPU-facing resources clarifies ownership and prepares PipelineKey to treat skinning as a first-class pipeline factor.

## What Changes

- Move `Skeleton`, `SkeletonUBO`, and `Bone` from `scene/components/` into `src/core/resources/` (new `skeleton.hpp` / implementation), and remove the old component files.
- Remove `IComponent` and `base.hpp`; delete `scene/components/` when empty. **Follow-on**: `Camera` and `DirectionalLight` currently inherit `IComponent` only for `getRenderResources()`; they must expose typed UBO accessors (e.g. `getUBO()`) and drop the interface so the codebase still compiles.
- **BREAKING**: `Skeleton` no longer inherits `IComponent`; `getRenderResources()` is replaced by `getUBO()` returning `SkeletonUboPtr`.
- Add `hasSkeleton()` and `getPipelineHash()` on `Skeleton` (hash reflects the boolean “has skinning” factor for pipeline identity).
- Introduce `getPipelineHash()` on `Mesh`, `RenderState`, and `ShaderProgramSet` (delegating to existing `getLayoutHash()` / `getHash()` where applicable); keep legacy names for internal reuse.
- Update includes and call sites (`object.hpp`, `mesh.hpp`, and any other references).

## Capabilities

### New Capabilities

- `skeleton-resource`: Core-layer contract for `Skeleton` as a resource (location, factory, `Bone` / UBO ownership, `getUBO`, `hasSkeleton`, no `IComponent`), plus removal of `IComponent` from scene types that used it (`Camera`, `DirectionalLight`) with equivalent UBO access.
- `resource-pipeline-hash`: Naming and behavior for `size_t getPipelineHash() const` on resources that participate in pipeline identity (`Mesh`, `RenderState`, `ShaderProgramSet`, `Skeleton`), and that `PipelineKey::build()` composes these values (REQ-002 may extend consumption).

### Modified Capabilities

- _(none — existing OpenSpec capabilities do not yet codify Skeleton or PipelineKey assembly; this change introduces new specs rather than deltas to `renderer-backend-vulkan`.)_

## Impact

- **Headers / sources**: `src/core/scene/components/skeleton.hpp`, `skeleton.cpp`, `base.hpp`; `src/core/scene/object.hpp`, `camera.hpp`, `light.hpp`; `src/core/resources/mesh.hpp`; `src/backend/vulkan/vk_renderer.cpp`; integration tests using `getRenderResources()`.
- **Semantics**: Callers use `getUBO()` instead of `getRenderResources()`; pipeline hashing uses a consistent `getPipelineHash()` name across participating resources.
- **Downstream**: REQ-002 (PipelineKey details) builds on `getPipelineHash()` from this change.
