# Unify Scene GPU Data for Realtime and Offline Rendering

## Goal

Unify LXEngine's realtime, bindless, offline software ray tracing, and future hardware ray tracing data model around one authoritative scene resource table. The current architecture already has `SceneResourceTable` as a bindless-ready resource ownership entry point; this requirement upgrades and completes that concept instead of introducing another builder, IR, or scene packing model.

The primary goal is architectural simplicity. Break changes are allowed when keeping compatibility would preserve duplicated scene data paths. Missing legacy capabilities must either be implemented in `SceneResourceTable` or recorded in this requirement as deferred capabilities. The current offline renderer MVP and the existing realtime/offline comparison output must remain consistent after the cleanup.

## Non-Goals

This requirement does not implement Vulkan hardware ray tracing, BLAS/TLAS creation, shader binding tables, hit shaders, or ray tracing pipeline dispatch. It prepares the data and integrator boundaries so that hardware ray tracing can be added later without introducing another scene representation.

It also does not preserve old offline-only scene packing classes as compatibility layers. If a capability cannot be carried across cleanly, the break is documented rather than hidden behind a parallel model.

## Core Principle

`SceneResourceTable` is the one scene GPU data contract.

There must not be a flow like:

```text
SceneResourceTable -> SceneGpuData -> OfflineRayScene -> renderer
```

That would recreate the duplication this work is intended to remove. Instead, `SceneResourceTable` itself owns the stable handles, lifetime, indexing, and GPU-facing records for geometry, materials, textures, lights, objects, cameras, and environment data. Other systems may create read-only views or derived acceleration structures from the table, but they must not create a second long-lived scene data model.

## Architecture

The target flow is:

```text
Scene / loaded assets
  -> SceneResourceTable
      owns geometry/material/texture/light/object/camera records
      owns stable handles and GPU-facing schema
      exposes read-only traversal views
  -> Vulkan scene upload layer
      uploads SceneResourceTable records to SSBOs/descriptors
  -> realtime traditional rendering
      consumes the same table records through pipeline/upload adapters
  -> realtime bindless rendering
      consumes table SSBOs directly
  -> offline software ray tracing
      builds a software BVH from table geometry/object records
      dispatches compute using table SSBOs plus software BVH buffers
  -> future offline hardware ray tracing
      builds BLAS/TLAS/SBT from the same table geometry/object/material records
```

`RenderSceneSnapshot` may remain only as a non-owning or short-lived traversal view over `SceneResourceTable`. It must not become a persistent owning data model.

## SceneResourceTable Responsibilities

`SceneResourceTable` must be extended until it can represent the data currently held by offline-only packing code. Missing capabilities should be added here instead of creating replacement IR classes.

Required responsibilities:

- Stable handle and generation tracking for geometry, meshes, materials, textures, lights, objects, cameras, and environment resources.
- GPU-facing geometry records, including position/index storage and attribute SSBO descriptions.
- Long-term movement toward position-only vertex input, with normals, UVs, tangents, skinning data, material IDs, and object IDs available from SSBO records.
- Material GPU records with PBR-first fields, legacy shading fields where still needed, material flags, and texture/resource indices.
- Object records with mesh/material references, transforms, inverse transforms, bounds, visibility, and debug flags.
- Camera, light, and environment records suitable for both realtime and offline consumers.
- Dirty generation or equivalent change tracking so backend upload code can update buffers without rebuilding unrelated data.
- Iteration APIs for backend upload, software BVH construction, realtime queues, and offline rendering.

The table may keep existing concepts such as `GeometryStorage`, `MeshBuffer`, and `MaterialInstance` only if they are part of the unified resource model. They must not be duplicated into offline-only equivalents.

## Classes and Concepts to Remove or Replace

The following concepts should not remain as formal long-term paths after this requirement:

- `OfflineSceneIR`
- `OfflineRayScene`
- Offline-only vertex/material/object packing records that duplicate `SceneResourceTable` records
- Offline-only scene compiler output that bypasses the normal scene/resource table

If implementation discovers an old field that cannot be represented cleanly in `SceneResourceTable`, it must be listed under "Breaking Changes and Deferred Capabilities" below rather than preserved through a compatibility model.

## Software BVH

The software BVH must remain. It is not duplicate scene data; it is a derived acceleration structure for the software offline ray tracer.

The existing BVH algorithm should be moved or renamed into a neutral scene/ray tracing acceleration layer, such as `SceneSoftwareBvh` or `CpuRayTracingAcceleration`. It must build from `SceneResourceTable` geometry/object records and store only acceleration nodes plus primitive references. It must not own or duplicate material, object, or geometry semantics.

Future hardware ray tracing will build Vulkan BLAS/TLAS from the same `SceneResourceTable` records. Software BVH and hardware AS are sibling acceleration backends sharing the same source data.

## Offline Renderer Integrators

`VulkanOfflineRenderer` should become an offline render coordinator rather than the owner of one hardcoded compute path.

Target integrator boundary:

- `SoftwareComputeOfflineIntegrator`
  - Current MVP implementation.
  - Consumes `SceneResourceTable` SSBO uploads and a `SceneSoftwareBvh`.
  - Dispatches the existing compute-style offline shader after its descriptor schema is updated to the unified records.
- `HardwareRtOfflineIntegrator`
  - Future implementation only.
  - Consumes the same table records.
  - Builds BLAS/TLAS/SBT and uses Vulkan ray tracing pipeline objects.

Integrator selection is explicit. The profile or scene configuration names the integrator, for example `software-compute` or future `hardware-ray-tracing`. The renderer must not automatically switch based on device capability, because offline rendering needs reproducibility across machines.

If an integrator name is unknown or unsupported, rendering fails with a clear error.

## Offline CLI and Scene Loading

If the offline CLI starts from `.scene.yaml`, it must load into the normal scene/resource-table path. It must not compile YAML into an offline-only IR.

The current offline scene compiler responsibilities should be split:

- Asset and document loading may remain in infra.
- Translation into long-lived scene data must populate `SceneResourceTable`.
- Offline-specific render settings remain profile data, not a separate scene representation.

## Vulkan Backend Responsibilities

The Vulkan backend uploads `SceneResourceTable` records and owns Vulkan objects only:

- `VulkanBuffer` resources for table records.
- Descriptor sets and descriptor layouts for traditional, bindless, and offline consumers.
- Dirty updates based on table generations.
- Future acceleration structure objects derived from table records.

The backend must not parse scene YAML, reinterpret material YAML, or build a separate semantic scene model. It adapts the unified table to Vulkan resources.

## Traditional Realtime Compatibility

Traditional realtime rendering may keep vertex-input pipelines in the short term, because existing shaders and mesh paths still depend on them. This is an adapter concern, not a second data model.

The adapter may read from `SceneResourceTable` and existing `GeometryStorage`/`MeshBuffer` entries to bind legacy vertex buffers. Longer term, vertex input should shrink toward position-only, while attributes move to SSBOs shared by bindless forward, offline software tracing, and hardware RT.

Compatibility must not be implemented by keeping a parallel offline data path.

## Breaking Changes and Deferred Capabilities

Break changes are allowed and should be documented in this requirement or its implementation notes. The implementation should favor a clean single data model over preserving every old offline capability.

Expected break categories:

- Removed offline-only C++ types and APIs.
- Removed tests that assert old `OfflineSceneIR` or `OfflineRayScene` behavior.
- Scene YAML fields that do not yet map cleanly to `SceneResourceTable`.
- Offline compare modes or material fields that cannot be represented during the cleanup.
- Any output or AOV capability temporarily lost while the data model is simplified.

Deferred capabilities must include a recovery rule: future work must restore them by extending `SceneResourceTable` and its upload/integrator consumers, not by recreating offline-only IR.

## Validation Requirements

The implementation is successful when:

- `SceneResourceTable` is the only authoritative scene GPU data source.
- Offline renderer no longer depends on `OfflineSceneIR` or `OfflineRayScene`.
- Software BVH is retained as a derived acceleration structure built from `SceneResourceTable`.
- The software compute offline renderer MVP still renders.
- Existing realtime/offline comparison output remains consistent with the current baseline.
- Tests cover CPU record layout, GLSL SSBO schema, table handle/generation behavior, software BVH construction from table data, offline integrator selection, and failure cases for invalid resources or unsupported integrators.
- Documentation lists any accepted break changes and deferred capabilities.

## Testing Strategy

Required tests:

- `SceneResourceTable` GPU record contract tests for geometry, material, texture, light, object, camera, and environment records.
- CPU/GPU schema alignment tests for record sizes and GLSL descriptor reflection.
- Software BVH tests that build from `SceneResourceTable` and verify primitive coverage and bounds.
- Offline renderer tests that render through `software-compute` using the unified table path.
- Realtime/offline comparison test using the existing comparison scene to guard output drift.
- Negative tests for missing mesh/material/object resources and invalid integrator names.
- Build tests proving removed offline-only APIs are no longer referenced.

## Implementation Notes

This is a single cleanup requirement, not a staged deprecation. The implementation may reorder code internally while the change is being developed, but the completed requirement must not leave transitional APIs, old and new scene data models, or offline-only compatibility layers side by side.

The existing `SceneResourceTable` comments already describe it as a bindless-ready resource model. This work should realize that design rather than inventing a replacement.
