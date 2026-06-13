# REQ-073-b Foundation Boundary Design

Date: 2026-06-13

## Decision

`REQ-073-b` is not CPU-only scaffolding. It must complete a foundation loop from source-reflected material data to backend-consumable bindless-ready table/staging data.

The requirement stops before renderer default-path consumption, indirect batching, and hard cut. Those remain in `REQ-073-d` and `REQ-073-e`.

## Required Scope In 073-b

`REQ-073-b` must deliver:

- source-local material storages keyed by source signature;
- continuous source-local material indices per source;
- material records containing factor values, texture slots, channel selectors, flags, source signature, and source-local index;
- stable default texture resources for `white`, `black`, and `flatNormal`;
- imported/default texture deduplication into texture table slots;
- bindless-ready texture, sampler, material storage, object, draw, and mesh/geometry tables;
- backend/GPU resource table consumption of those tables into stable slot/staging data;
- diagnostics that map backend slot/staging records back to resource identity, source signature, and source-local material index;
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

## Current Code Pressure

The code already has a partial source-local storage shape:

- `SceneResourceTableUploadView` exposes `sourceMaterialRecords` and `sourceMaterialStorages`.
- `MaterialInstance` carries source URI, reflection hash, source signature, and contract reflection.
- `MaterialContractPackResult` can carry default texture slots and diagnostics.

But the current shape is not enough for `REQ-073-b` completion:

- source-local records are still mostly empty;
- draw/object upload records primarily point at legacy material indices;
- old `SceneGpuMaterialRecord` remains the concrete PBR upload record;
- default texture resources are not yet proven as stable resource identities/table slots in this path;
- backend/GPU resource table consumption of the new material/object/draw tables is not yet the validation target.

The implementation plan should therefore extend the existing scaffolding rather than start over.

## Non-goals

`REQ-073-b` should not make the realtime renderer default path consume the new tables. It only proves the tables are complete and backend-consumable. Once that is true, `REQ-073-d` can make RenderWorkQueue and geometry passes consume them for batching, and `REQ-073-e` can remove the old fallback path.
