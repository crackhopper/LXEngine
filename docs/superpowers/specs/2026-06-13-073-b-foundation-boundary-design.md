# REQ-073-b Foundation Boundary Design

Date: 2026-06-13

## Decision

`REQ-073-b` is not CPU-only scaffolding. It must complete a foundation loop from source-reflected material data to backend-consumable bindless-ready table/staging data.

The requirement stops before renderer default-path consumption, indirect batching, and hard cut. Those remain in `REQ-073-d` and `REQ-073-e`.

## Required Scope In 073-b

`REQ-073-b` must deliver:

- source storages keyed by source signature;
- stable `sourceStorageIndex` values for storage table rows;
- continuous source-local material indices per source storage;
- source-reflected material payloads containing factor values, texture slots, channel selectors, and flags;
- material refs and storage headers containing source storage identity plus source-local index;
- stable default texture resources for `white`, `black`, and `flatNormal`;
- imported/default texture deduplication into texture table slots;
- bindless-ready texture, sampler, material storage, object, draw, and mesh/geometry tables;
- backend/GPU resource table consumption of those tables into stable slot/staging data;
- diagnostics that map backend slot/staging records back to resource identity, `sourceStorageIndex`, source signature, and source-local material index;
- fail-fast behavior for missing source signatures, default texture slots, invalid texture slots, layout invariant violations, invalid source-local material indices, and unsupported backend table upload.

This means a backend test must be able to consume the upload view and prove that the resource table can create the bindless-ready table/staging state. A CPU span with plausible data is not enough.

## Explicitly Moved Out

The following work is not deferred vaguely; each item has an owning follow-up requirement:

| Work moved out of 073-b | Owner |
|---|---|
| RenderPath material source shader variants, `LX_MATERIAL_CONTRACT_SOURCE` compile injection, final shader reflection, and `techniques/...` to `render_paths/...` migration | `REQ-073-c` |
| RenderWorkQueue / geometry pass default consumption of bindless tables for indirect-capable work items, batch compatibility signatures, and split diagnostics | `REQ-073-d` |
| Removal of old `SceneGpuMaterialRecord` / `MaterialUBO` realtime truth, per-material descriptor fallback, hidden debug/default-material fallback, and Helmet/BMW realtime smoke | `REQ-073-e` |
| OfflineRT RenderPathGraph compute path and offline config hard cut | `REQ-073-f` / `REQ-073-g` |

## Current Implementation Status

After Task 7, the code has moved past the initial scaffolding:

- `SceneResourceTableUploadView` exposes `sourceMaterialRecords`, `sourceMaterialStorages`, and `materialRefs`.
- Source-contract material draws use `materialIndex == u32_max` plus a valid `materialRefIndex`; the legacy `materialIndex` field only remains for old-path materials until `REQ-073-e`.
- `MaterialInstance` carries source URI, reflection hash, source signature, contract reflection, and table-owned texture handles for material parameters when registration converts pending textures.
- `MaterialContractPackResult` packs source-reflected bytes and default/direct/canonical texture slots.
- `SceneResourceTable` owns builtin `white`, `black`, and `flatNormal` texture resources.
- `VulkanGpuResourceTable::uploadSceneBindlessTables()` consumes the upload view into texture bindless slots, per-source material buffers, object/draw/mesh buffers, and position/index/primitive/attribute geometry buffers.

Final `REQ-073-b` verification has passed. The remaining material migration
boundary is outside this requirement:

- old `SceneGpuMaterialRecord` still exists only for legacy/default realtime paths and must not become positive Material v3 truth again.

`REQ-073-d` and `REQ-073-e` own the later renderer consumption and old-path
removal work.

## Source Storage Terminology

`sourceSignature` and `sourceLocalMaterialIndex` are not duplicate identities.

The clean reference shape is:

```text
sourceStorageIndex + sourceLocalMaterialIndex
```

`sourceStorageIndex` selects a row in the source storage table. That row owns the `sourceSignature`, source URI, reflection hash, and storage ABI hash. `sourceLocalMaterialIndex` selects a material record inside that storage.

For example:

```text
sourceStorageIndex = 0
  sourceSignature = matte-v1
  sourceLocalMaterialIndex 0 -> red_paint.material
  sourceLocalMaterialIndex 1 -> blue_paint.material

sourceStorageIndex = 1
  sourceSignature = metal-v1
  sourceLocalMaterialIndex 0 -> chrome.material
```

Object/draw records should therefore store or resolve to `sourceStorageIndex` plus `sourceLocalMaterialIndex`. They should not repeatedly store `sourceSignature` as a second material reference. Diagnostics can recover the signature through the storage header.

## Non-goals

`REQ-073-b` should not make the realtime renderer default path consume the new tables. It only proves the tables are complete and backend-consumable. Once that is true, `REQ-073-d` can make RenderWorkQueue and geometry passes consume them for batching, and `REQ-073-e` can remove the old fallback path.
