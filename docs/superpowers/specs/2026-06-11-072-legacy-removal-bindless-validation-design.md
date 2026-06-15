# REQ-072-D First: Legacy Removal And Bindless Validation Design

## Goal

Start `REQ-072` with the highest-risk closure issue: remove legacy material/rendering implementation paths from the default runtime and prove the bindless architecture is usable end to end.

This design intentionally prioritizes `REQ-072` item D before package completion and full helmet/BMW validation. The first successful outcome is not a full BMW package pipeline. The first successful outcome is a default validation scene that renders through bindless scene data and indirect-capable work submission without falling back to old PBR material truth, per-material descriptor binding, or non-bindless draw submission.

## Current Problem

The `REQ-071` implementation added useful foundations, but several paths are still transitional:

- Editor and scene helpers still expose `MaterialUBO.baseColor`, old PBR factors, and node material baseColor override as default concepts.
- `VulkanRealtimeRenderer::drawPassQueue()` can silently fall back from indirect batches to per-item execution.
- `VulkanCommandBuffer::bindResourcesWithLayout()` still binds per-item descriptors and push constants for normal draw work.
- `VulkanGpuResourceTable` is currently an API shell rather than the single backend resource truth.
- Existing bridge audit checks synthetic queue behavior more than real renderer behavior.

Those paths make it impossible to prove that `REQ-071` migrated the default rendering contract. They must either become unreachable dead code, be deleted after caller migration, or be explicitly retained as non-default debug tooling with tests proving validation does not use them.

## Scope

This phase covers:

- Removing legacy default material truth from migrated validation paths.
- Migrating default validation rendering to bindless scene data.
- Making fallback to non-bindless draw submission fail-fast in validation mode.
- Proving `SceneResourceTable -> GPUResourceTable -> shader global arrays -> draw` works on a minimal Vulkan realtime scene.
- Applying `dead-code-prune` discipline to delete obsolete helper chains after callers move to the bindless path.
- Updating `REQ-071` / `REQ-072` docs to distinguish removed code, retained debug-only code, and remaining later work.

This phase does not complete:

- Full `.lxpkg` section/chunk streaming restore.
- Full helmet/BMW material sphere matrix.
- Shadows, IBL, transparent/glass correctness.
- Complete removal of every demo asset that still uses old material names, unless it participates in default validation or migrated runtime paths.

## Design Principles

1. **Bindless is the default contract.** Migrated validation scenes must get material/object/texture data from global typed arrays and bindless slots, not from per-material descriptor updates.
2. **Fallback is explicit failure.** In validation mode, any draw item that cannot enter the bindless path reports unsupported or contract violation. It must not silently call the old per-item submission path.
3. **Legacy code is deleted by proof.** A path is removed only after inbound callers are inventoried and either migrated to the canonical bindless path or proven unreachable.
4. **Debug-only means test-proven default-off.** If a legacy helper survives for editor/demo diagnostics, it must be explicitly marked and excluded from validation tests.
5. **Use a minimal scene first.** A small Material v2 scene is the acceptance vehicle before helmet/BMW complexity is reintroduced.

## Target Architecture

### Canonical Default Path

```text
Material v2 scene
  -> SceneResourceTable typed resources
  -> SceneResourceTableUploadView
  -> GPUResourceTable allocation and bindless slot mapping
  -> global scene/material/object/texture buffers
  -> RenderWorkQueue batchable work
  -> Vulkan validation executor
  -> shader reads object/material indices from global arrays
```

The draw path may still use CPU-generated indirect commands, but the material/object identity must be in bindless/global storage rather than per-draw material descriptor state.

### Legacy Paths

Legacy paths fall into three categories:

- **Redundant dead path:** still has callers, but callers should migrate to bindless equivalents. Remove after migration and second-order reachability search.
- **Unreachable dead path:** no live inbound callers. Remove along with callees whose only callers are inside the dead set.
- **Justified debug path:** retained only if it has a concrete editor/demo purpose, is not part of validation, and is guarded by explicit naming or flags.

Examples of suspicious roots to classify during implementation:

- `MaterialUBO.baseColor` editor command and inspector flows.
- glTF/PBR conversion into `MaterialUBO.baseColorFactor`, `metallicFactor`, and `roughnessFactor` when used as runtime truth.
- per-item descriptor and push-constant binding paths used by default raster draws.
- `drawPassQueue()` fallback from incomplete batch coverage to per-item execution.
- bridge tests that only prove synthetic queue behavior.

## Components

### 1. Bindless Validation Mode

Add or reuse a validation-mode flag derived from `SceneValidationProfile`. When enabled:

- renderer rejects non-bindless draw work for migrated passes;
- renderer rejects per-material descriptor binding for migrated material/effect resources;
- renderer emits a structured diagnostic naming pass, material, object, and reason;
- tests assert that diagnostics occur instead of fallback rendering.

Validation mode is stricter than normal editor mode. Normal editor debug tools may keep transitional helpers only if they are outside the migrated validation path.

### 2. Real Bindless Smoke Scene

Create a minimal scene using:

- Material v2 envelopes;
- explicit Forward technique;
- at least two objects;
- at least two material instances sharing one template;
- at least one shared texture/sampler resource;
- one camera and one direct light;
- no shadows, IBL, transparency, or package dependency.

Expected behavior:

- shared texture maps to one bindless slot;
- material instances have separate material indices;
- objects have object indices;
- one compatible pipeline can render both objects;
- output is non-black;
- validation metadata proves bindless/global-array execution.

### 3. GPUResourceTable Reality Check

This phase does not need the final package cache model, but it must stop treating `VulkanGpuResourceTable` as a fake success signal.

Minimum requirements:

- slot allocation is stable and deduplicated per CPU resource identity;
- buffer/image/sampler requests map to real backend resources or explicit unsupported diagnostics;
- pipeline lookup has observable hit/miss behavior;
- progress reports can distinguish pending/uploading/ready/failed for validation resource upload.

If a final integration with `VulkanResourceManager` is too large for this phase, introduce an adapter boundary that delegates to existing real resource objects while keeping `IGpuResourceTable` as the public validation contract.

### 4. Renderer Fallback Enforcement

Refactor or guard `drawPassQueue()` so validation-mode behavior is deterministic:

- if all draw items are bindless batch compatible, execute the bindless path;
- if a migrated pass produces a non-compatible draw item, return a validation error;
- legacy per-item execution is not reachable from validation-mode migrated passes;
- debug/UI overlay passes may use separate explicit paths and are not counted as migrated material validation passes.

The bridge audit must inspect actual renderer execution decisions, not only `RenderWorkQueue::compileIndirectBatches()`.

### 5. Dead Code Prune Workflow

Each legacy removal starts from a named root and records:

- inbound callers;
- whether the root is redundant, unreachable, or justified debug;
- canonical bindless replacement if redundant;
- callers migrated away;
- dependent callees removed because all inbound callers are inside the dead set;
- functions intentionally kept due to live callers or ambiguous reachability.

Deletion order:

1. migrate callers to bindless path;
2. rerun inbound searches;
3. delete unreachable root and dead callee slice;
4. search again for second-order dead code;
5. build and run relevant tests.

This avoids deleting string-registered commands, virtual overrides, reflection hooks, or test-only entry points by accident.

## Data Flow

1. `SceneValidationProfile` selects strict validation mode.
2. Scene load creates `SceneResourceTable` material, texture, object, camera, and light resources.
3. Upload view exports typed arrays and handle-to-index mappings.
4. `GPUResourceTable` allocates buffers and bindless slots from typed resources.
5. Render work stores object/material indices rather than per-draw material descriptors.
6. Renderer checks migrated-pass compatibility before drawing.
7. Shader reads scene data through global arrays.
8. Validation output includes non-black color plus metadata proving the bindless path.

## Error Handling

Validation-mode failures are explicit and structured:

- missing bindless capability;
- material still requiring legacy parameter truth;
- draw item requiring per-draw push constants for migrated material data;
- descriptor resource not representable in global bindless tables;
- incomplete batch coverage for a migrated pass;
- shader layout mismatch with global scene/material ABI.

Each error includes pass id, object/material identity when available, and the failing contract.

## Testing Strategy

### Contract Tests

- `VulkanGpuResourceTable` slot deduplication and real-resource allocation.
- pipeline cache observable hit/miss.
- validation-mode fallback rejection.
- bridge audit over actual renderer decision path.

### Rendering Smoke

- minimal Material v2 bindless scene renders non-black under xvfb.
- two material instances share template and pipeline but use different material indices.
- shared texture URI maps to one bindless slot.

### Dead-Code Tests

- legacy command/helper roots removed only after caller migration.
- default validation does not call old PBR material loader paths.
- default validation does not bind per-material descriptor sets for migrated passes.
- retained debug-only helpers are default-off.

### Regression Gates

Run after this phase:

```bash
cmake --build build --target BuildTest
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

If a remaining failure is unrelated to legacy removal or bindless validation, record it in `REQ-072` with exact command and failure.

## Documentation Updates

Update:

- `notes/requirements/072-071-closure-audit-and-validation-fixes.md` to mark this phase as first.
- `notes/requirements/071-d-gpu-resource-table-pipeline-cache-and-upload-tasks.md` with actual bindless/default-path status.
- `notes/requirements/071-f-rendering-equivalence-helmet-bmw-validation.md` with validation-mode bridge audit outcome.
- `docs/superpowers/plans/2026-06-10-071-material-resource-rendering-pipeline.md` with the fact that 072 took over legacy removal and bindless proof.

## Acceptance Criteria

- No migrated validation path uses old PBR material truth as runtime source.
- No migrated validation path silently falls back to non-bindless per-item draw submission.
- Minimal bindless Vulkan validation scene renders non-black.
- Bindless slot sharing and material/object index lookup are verified by tests.
- Legacy removal report lists deleted roots, migrated callers, retained debug-only paths, and ambiguous paths intentionally kept.
- Full build passes, and relevant auto/video tests are run or failures are recorded with exact diagnostics.
