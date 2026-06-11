# REQ-071-g Legacy Boundary Removal Audit Design

## Goal

`REQ-071-g` is the hard-cut cleanup step for the `REQ-071` material,
resource, render-graph, and bindless migration. Its job is not to add another
compatibility bridge. Its job is to delete every default/runtime path that can
route migrated validation back through legacy material-local techniques,
hardcoded pass construction, `materialTag`, `MaterialUBO`, non-bindless
submission, or per-draw CPU push-constant data.

The implementation may temporarily break build or tests while the old entry
points are deleted and remaining coupling is exposed. Each phase must record
which boundary currently fails and why. The final implementation plan must
restore a verified default path with the static and runtime audits passing.

## Current Context

The repository already has useful `REQ-071` foundations:

- Material v2 assets such as `assets/materials/pbr.material` and
  `assets/materials/pbr_gold.material`.
- RenderPathGraph and RenderFeature assets such as
  `assets/render_paths/forward_main.render-path.yaml` and
  `assets/effects/tone_mapping.render-feature.yaml`.
- FrameGraph source/target DAG build support.
- SceneResourceTable typed resource handles and upload-view records.
- Strict bindless validation tests.

The default/runtime code still contains legacy entry points:

- `GenericMaterialLoader` still recognizes material-local technique fields and
  old root parameter/resource forms.
- `VulkanRealtimeRenderer` still contains a built-in default forward graph and
  hardcoded pass insertion for Forward, Deferred, PostProcess, DebugOverlay,
  and related fullscreen work.
- scene documents, output profiles, editor commands, offline loading, and the
  PBRT converter still support `materialTag`.
- production shaders still declare `MaterialUBO` and old PBR factors.
- GPU material records still read old parameter-buffer or binding fallback
  data.
- `BindlessSubmissionDecisionKind::LegacyPerItem` and per-item draw submission
  still let default rendering avoid bindless/indirect coverage.
- `RenderWorkItem::raster.drawData`, `PerDrawData`, upload-plan push constants,
  and Vulkan command-buffer push constants still carry default per-draw CPU
  data.

## Non-Goals

- Do not implement the full `REQ-071-d` GPUResourceTable, descriptor indexing,
  pipeline cache, upload task, or indirect backend.
- Do not implement the `REQ-071-e` scene package.
- Do not complete `REQ-071-f` Helmet/BMW equivalence validation.
- Do not preserve old assets, old material-local techniques, old tag switching,
  or old descriptor submission behind a compatibility flag.
- Do not add `legacy`, `compat`, `allowOldMaterial`, `fallback`, or debug
  bypass switches for default paths.

## Architecture

The target default data flow is:

```text
Material v2 scene/profile
  -> SceneResourceTable typed handles
  -> active RenderPathGraph asset/resource
  -> FrameGraph passes from graph ids
  -> SceneResourceTableUploadView material/object/draw records
  -> bindless/indirect-ready RenderWorkQueue
  -> Vulkan submit only if the migrated batch is complete
```

Nothing in this flow may fall back to the old material loader, `materialTag`,
hardcoded pass enums as graph truth, `MaterialUBO`, per-draw CPU byte blobs, or
per-item descriptor submission.

The design divides the hard cut into five boundaries.

### 1. Material Contract Boundary

`.material` files in runtime/default paths only accept
`schema: lxe.material.v2`. A material is a PBRT surface envelope plus typed
resource handles and dependency metadata.

`GenericMaterialLoader` may remain only as a thin wrapper around the Material v2
parser. It must reject non-v2 material files and v2 files that contain render
flow or old parameter truth.

Default material truth may contain:

- PBRT BSDF type.
- PBRT envelope parameters.
- typed texture, spectrum, BSDF table, and material-reference handles.
- render class or tags used only by graph filters.
- dirty/version/dependency metadata.

Default material truth must not contain:

- shader URI.
- pass names.
- render state.
- `defaultTechnique`, `techniques`, `passes`, `variants`, or `variantRules`.
- root `parameters` or root `resources`.
- old PBR factors such as `baseColorFactor`, `metallicFactor`,
  `roughnessFactor`, or `ao`.
- `ParameterBuffer` data used as material truth.

Old fields are fatal schema errors. The implementation should report unknown
field paths rather than preserving deleted field constants in production error
strings.

### 2. Scene Identity Boundary

Default scene, profile, editor, offline, and converter paths no longer support
`materialTag`.

The hard cut removes:

- scene YAML and render/output profile read/write support for `materialTag`.
- editor session commands and JSON fields that switch material tags.
- `Scene::setActiveMaterialTagForRenderables`.
- `MaterialComponent` active tag state and tagged material maps.
- offline scene-loader selection by tag.
- PBRT converter output that emits realtime/offline material tags.

After the cut, one renderable references one concrete `MaterialInstance` handle.
Realtime, deferred, offline, and validation differences are selected by
RenderPathGraph, pass filters, RenderClass, BSDF type, and concrete material
identity. They are not selected by swapping another material through a tag.

Helmet and BMW validation scenes must be migrated to direct material
references, and scene save paths must not output tag fields.

### 3. Render Graph Boundary

The active render flow comes from a RenderPathGraph asset/resource. Backend code
must not synthesize a default production graph.

The hard cut removes:

- `VulkanRealtimeRenderer::makeDefaultForwardRenderPathGraph()`.
- default `initScene()` insertion of hardcoded Forward, Deferred, PostProcess,
  DebugOverlay, Bloom, or fullscreen passes.
- `deferredMode` inference of GBuffer, Forward, or PostProcess graph shape.
- default queue, camera resource, fullscreen item, target, source, or producer
  reconstruction from pass enum names.

FrameGraph pass identity comes from RenderPathGraph pass ids. Sources, targets,
shader URI, render state, producers, and filters come from the graph. A pass
not declared by the graph does not execute. Missing graph fields or missing
producers are validation errors.

`Pass_*` constants may remain only where they are explicit test fixtures or
non-default utilities. Production default graph construction must not depend on
them as source-of-truth names.

### 4. GPU Material/Data Boundary

Production shaders and CPU upload records no longer use `MaterialUBO` or old
PBR factor truth.

The hard cut removes:

- `MaterialUBO` declarations from production Forward and Deferred PBR shaders.
- shader reads from old fields such as `baseColorFactor`, `metallicFactor`, and
  `roughnessFactor`.
- `SceneGpuMaterialRecord` fallback reads from `MaterialUBO`, `SurfaceParams`,
  old texture binding names, or generic shader parameter buffers.
- default material editing/upload paths that treat `ParameterBuffer` as
  migrated material truth.

Shader data comes from fixed system ABI indices and global material/object
records derived from PBRT envelopes and typed resource handles. The GPU material
record is a derived cache, not a second editable material model.

If `ParameterBuffer` survives for post-process or a non-material debug path, it
must be disconnected from default surface-material truth and excluded from the
audited runtime path.

### 5. Submission Boundary

Default migrated render work no longer falls back to per-item descriptor
submission or per-draw push constants.

The hard cut removes:

- `BindlessSubmissionDecisionKind::LegacyPerItem`.
- legacy branches in `decideBindlessSubmission()`.
- `VulkanRealtimeRenderer::drawPassQueue()` fallback that calls
  `cmd.executeWorkItem(item)` after incomplete bindless batch coverage.
- `RenderWorkQueue::compileIndirectBatches()` behavior that silently skips
  unbatchable work and still lets a non-empty migrated pass render.
- `RenderUploadPlan` push-constant collection for default draw data.
- Vulkan command-buffer default push constants from `raster.drawData`.
- backend helpers that recover object transforms from per-draw CPU data.

An empty queue is a no-op. A non-empty migrated queue that cannot produce
complete bindless/indirect-ready work is a fatal or unsupported diagnostic.
That diagnostic should name pass, object, material, and the failed contract when
that identity is available.

## Deletion Order

The implementation plan should delete old entry points before adding bridges.
Build and test failures are acceptable during the phase that exposes remaining
coupling, but the failure must be classified under one of the five boundaries.

1. Define the static audit test and forbidden production symbols. It may start
   red.
2. Cut the material contract boundary: v2-only loader, no material-local
   technique parsing, no root legacy parameter/resource truth.
3. Cut the scene identity boundary: remove `materialTag` model, migrate Helmet
   and BMW validation scenes, and stop PBRT converter tag output.
4. Cut the render graph boundary: remove backend default graph construction and
   hardcoded default pass insertion.
5. Cut the GPU material/data boundary: remove `MaterialUBO` shaders and upload
   fallback from old parameter buffers.
6. Cut the submission boundary: remove `LegacyPerItem`, per-draw data, and
   default push-constant submission.
7. Restore the minimal default validation path through Material v2,
   RenderPathGraph, SceneResourceTableUploadView, and complete
   bindless/indirect-ready render work.
8. Make the static and runtime audits green.

## Static Audit

Add `test_071g_legacy_boundary_removal`.

The audit scans:

- `src/core`
- `src/infra`
- `src/backend`
- `src/demos/lxe_editor`
- `assets/materials`
- `assets/shaders`
- `assets/scenes`
- `assets/render_paths`
- `assets/effects`
- `data/scenes`
- `src/tools/lxe_pbrt_scene_convert`

The audit forbids default/runtime appearances of:

```text
defaultTechnique
MaterialUBO
baseColorFactor
metallicFactor
roughnessFactor
materialTag
setActiveMaterialTag
activeMaterialTag
BindlessSubmissionDecisionKind::LegacyPerItem
LegacyPerItem
raster.drawData
PerDrawData
makeDefaultForwardRenderPathGraph
```

Allowed appearances are limited to:

- `notes/requirements/`, `notes/requirements/finished/`, and docs/specs.
- `src/test/integration/` negative fixtures that verify old input is rejected.
- third-party, generated, or external directories.

Production error messages should avoid these forbidden strings where possible.
They should identify the invalid schema path or unknown field class instead of
keeping deleted-field constants alive.

## Runtime Audit

Add a runtime audit that builds a minimal Material v2 scene and exercises the
default validation path.

It proves:

- active RenderPathGraph comes from an asset or SceneResourceTable resource.
- FrameGraph pass ids come from the RenderPathGraph.
- scene/material load does not call material-local technique parsing.
- render work does not contain per-draw CPU data.
- render work does not contain a material-local `MaterialUBO` descriptor.
- bindless validation is default for migrated passes.
- incomplete indirect/bindless coverage fails instead of submitting per item.
- scene/profile load and save do not contain tag-based material switching.

The runtime audit is intentionally smaller than Helmet/BMW equivalence. It is a
contract proof for the default migrated path.

## Error Handling

The error model is fail-fast:

- Old material fields are fatal schema errors.
- Missing RenderPathGraph, pass, shader, source, target, render state, filter,
  or producer data is a validation error.
- Unsupported BSDF/render-class/pass combinations are validation errors. They
  do not synthesize old material data.
- A non-empty migrated queue that cannot form complete bindless/indirect-ready
  work is an unsupported or fatal diagnostic.
- Editor, debug, or offline functions that depended on old material tags or old
  material parameters must be migrated to the new contract or removed.

No compatibility switch may make a default validation path succeed through the
old behavior.

## Testing Strategy

Focused tests:

- Static legacy symbol audit.
- Material v2 parser rejects non-v2 and material-local technique/root legacy
  fields.
- scene document load/save rejects or omits tag-based material switching.
- PBRT converter output has direct material references and no tag profiles.
- RenderPathGraph validation rejects missing graph fields and missing
  producers.
- FrameGraph compile uses graph pass ids and source/target DAG order.
- shader reflection for production PBR shaders contains no material-local
  uniform block.
- GPU material record generation uses PBRT envelopes and typed handles only.
- bindless submission decision has no legacy per-item kind.
- default migrated render queue rejects incomplete batch coverage.
- upload plan and command-buffer default paths no longer collect or submit
  per-draw push constants.

Verification commands for the final implementation should include the normal
Linux build/test gates used by the repository, plus video-device tests through
`xvfb-run` when the runtime audit needs Vulkan/SDL.

## Relationship To Existing Specs

This design is narrower than the master `REQ-071` design and stricter than the
existing `REQ-072-D First: Legacy Removal And Bindless Validation Design`.

`REQ-072` can still prove bindless validation end to end, but `REQ-071-g`
defines the hard boundary removal that must happen first: old default entry
points are deleted, not hidden behind strict mode, debug mode, or validation
mode toggles.

`REQ-071-d` can then implement the real GPUResourceTable and descriptor table
against a default path that no longer has legacy escape routes.

## Acceptance Criteria

- Default/runtime material loading is v2-only.
- Default/runtime scenes and profiles do not use material tags.
- Backend production FrameGraph setup comes from RenderPathGraph assets or
  resources, not built-in graph functions.
- Production shaders and material upload records do not use `MaterialUBO` or
  old PBR factor truth.
- Default migrated render work has no per-draw CPU data path.
- Default migrated non-empty queues cannot silently fall back to per-item
  descriptor submission.
- Static audit passes for the forbidden production symbols.
- Runtime audit proves the minimal default path uses Material v2,
  RenderPathGraph, typed upload data, and strict bindless/indirect-ready
  submission behavior.
