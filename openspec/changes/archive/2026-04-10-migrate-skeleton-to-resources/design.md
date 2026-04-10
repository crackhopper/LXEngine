## Context

Mesh and material types already live under `src/core/resources/`. `Skeleton`, `SkeletonUBO`, and `Bone` still live in `src/core/scene/components/` and implement `IComponent`, whose sole method is `getRenderResources()`. The same interface is implemented by `Camera` and `DirectionalLight` for UBO discovery. Removing `IComponent` (per REQ-001 R2) therefore touches every implementer, not only `Skeleton`.

`docs/requirements/001-skeleton-to-resources.md` also requires a uniform `getPipelineHash()` name across resources that feed pipeline identity; `PipelineKey` is not yet present in the tree (REQ-002 will consume these hashes).

## Goals / Non-Goals

**Goals:**

- Move skeleton-related types into `src/core/resources/` and align their API with other resources (`getUBO()`, explicit skinning flag via `hasSkeleton()`).
- Delete `IComponent` and `components/base.hpp`; remove the `scene/components/` directory once empty.
- Provide `getPipelineHash()` on `Mesh`, `RenderState`, `ShaderProgramSet`, and `Skeleton`, delegating to existing `getLayoutHash()` / `getHash()` where applicable.
- Update all call sites (scene, Vulkan renderer, integration tests) to stop using `getRenderResources()`.

**Non-Goals:**

- Full `PipelineKey::build()` implementation and caching (tracked under REQ-002).
- Changing bone math, UBO packing, or skinning shader contracts beyond what is required for API moves and hashing.

## Decisions

1. **UBO access replaces `IComponent`**  
   **Choice**: Each former `IComponent` type exposes a typed `getUBO()` (or existing `ubo` member remains public where it already is) instead of a virtual `std::vector<IRenderResourcePtr>`.  
   **Rationale**: The interface existed only to unify UBO retrieval; typed accessors are clearer and avoid heap allocations for single-UBO types.  
   **Alternative considered**: Introduce a narrower interface (e.g. `IUniformProvider`) — rejected as unnecessary indirection for three concrete types.

2. **`Skeleton::getPipelineHash()` encodes a boolean skinning factor**  
   **Choice**: Hash reflects whether the instance participates in skinned pipelines (per REQ R5: based on `hasSkeleton()` semantics).  
   **Rationale**: Matches REQ-001’s intent that skinning is a discrete pipeline switch (vertex layout, extra UBO, push constants).  
   **Alternative considered**: Hash full bone count — deferred until a requirement demands finer granularity.

3. **`getPipelineHash()` on `Mesh` / `RenderState` / `ShaderProgramSet`**  
   **Choice**: Add `getPipelineHash() const` that forwards to `getLayoutHash()` or `getHash()` without removing the old methods.  
   **Rationale**: REQ R6 requires a single readable name at call sites; keeps backward-compatible internal use.

4. **File split**  
   **Choice**: New `src/core/resources/skeleton.hpp` (+ `.cpp` if non-inline implementation is moved out of header).  
   **Rationale**: Matches Mesh/Material layout; keeps `LX_core` resource types discoverable under one directory.

## Risks / Trade-offs

- **[Risk] Missed include or call site after moving types** → **Mitigation**: Grep for `components/skeleton`, `IComponent`, `getRenderResources` after edits; run full integration build.
- **[Risk] Proposal doc R4 under-listed Camera/Light/renderer** → **Mitigation**: Explicit tasks for `camera.hpp`, `light.hpp`, `vk_renderer.cpp`, and `test_vulkan_command_buffer.cpp`.
- **[Trade-off] `PipelineKey` not in repo yet** → Hashes are API-ready; wiring is intentionally deferred to REQ-002.

## Migration Plan

1. Add new resource header and `getPipelineHash()` / `getUBO()` APIs alongside old types (or move in one step with immediate compile fixes).
2. Update scene and backend to use `getUBO()` everywhere.
3. Remove `IComponent`, old skeleton files, and `components/` directory.
4. Run `ninja BuildTest` (or project’s full test target) and fix regressions.

**Rollback**: Revert the branch; no runtime data migration (source-only refactor).

## Open Questions

- None blocking: REQ-002 will define exact `PipelineKey` composition order and collision handling.
