# REQ-073-e Opaque Indirect Batching And REQ-073-f Transparent BMW Handoff Design

Date: 2026-06-14

## Decision

`REQ-073-e` is the hard landing for realtime opaque geometry batching and
backend indirect draw submission.

The previous `REQ-073-f` realtime hard-cut scope is no longer a separate cleanup
phase for `REQ-073-a/b/c` compatibility. That hard cut moved forward into
`REQ-073-d`, which owns the `techniques/...` to `render_paths/...` migration,
legacy material-source route deletion, and ordinary positive-test cleanup.

The new split is:

- `REQ-073-e`: opaque realtime geometry batching, backend indirect draw, old
  opaque geometry submission replacement, diagnostics, and Helmet smoke.
- `REQ-073-f`: transparent queue sorting/batching, glass material support,
  BMW converter/shader coverage, transparent RenderPathGraph pass, and BMW
  smoke.

This keeps `073-e` focused on the batching system that must exist before BMW
glass/transparent work can be meaningful, while keeping the BMW-specific
material and transparent pass expansion out of the Helmet/opaque implementation
cycle.

## Current Context

`REQ-073-c` established the current material-source shader variant boundary:

```text
PipelineKey = MaterialTypeVariant + RenderPathNodeSignature
```

That is a current pipeline-cache implementation fact, not the target opaque
batch key for `073-e`.

`REQ-073-d` is executing the hard cut for the old material/source compatibility
surface. After `073-d`, positive realtime material-source paths must use:

- `render_paths/...` shader URIs;
- RenderPathGraph / RenderPathNode pass ownership;
- final source-variant shader reflection;
- typed SceneResourceTable material/draw/object indices.

The current `REQ-073-e` code surface already has some scaffolding:

- `RenderWorkQueue::compileIndirectBatches()`;
- `RenderIndirectBatch`;
- `drawRecordIndex` and `materialRefIndex` on the current pre-batch draw
  payload;
- `PipelineKey::build(materialTypeVariant, renderPathNodeSignature)`;
- Vulkan submission path that calls `compileIndirectBatches()` for bindless
  batch submission.

But the current batch compiler is still transitional:

- it silently skips unsuitable draw items;
- it batches using `DescriptorResourceList` equality;
- it can split batches on per-material resource identity instead of table
  index/range compatibility;
- diagnostics only report uncovered items after the fact;
- tests still contain a positive expectation that descriptor changes split
  indirect batches.

`073-e` replaces that transitional behavior with a first-class opaque batch
analysis and submission contract. It also removes the current
`RenderWorkKind`/`kind` concept from the opaque geometry path. `kind` is an implementation
artifact of the old "one union-like RenderWorkItem for every possible
submission" model; it is not a rendering contract and must not survive as an
opaque batching concept.

## REQ-073-e Scope

`073-e` includes:

- opaque Forward/Deferred geometry work item readiness for indirect draw;
- table-index/range based batch compatibility;
- a diagnostic batch compiler result rather than silent skips;
- backend indirect draw submission as the realtime opaque geometry default;
- deletion of old opaque geometry direct/per-item success paths that the new
  indirect path replaces;
- Helmet realtime smoke on the new opaque path.

`073-e` excludes:

- BMW smoke;
- glass material support;
- transparent queue sorting;
- transparent RenderPathGraph pass;
- OfflineRT;
- package, BC7, and pipeline cache serialization.

`073-e` may mention transparent work only as the explicit handoff to `073-f`.
It must not add a partial transparent system.

## REQ-073-e Architecture

The positive opaque path is:

```text
RenderPathGraph opaque node
  -> final source-variant shader reflection
  -> material pipeline signature for this node
  -> SceneResourceTableUploadView
  -> OpaqueGeometryDrawCandidate with typed material/draw/object/mesh indices
  -> OpaqueBatchCompiler
       batches or rejects with diagnostics
  -> Vulkan indirect draw submission
```

### Work Item Readiness

An opaque geometry draw candidate is indirect-ready only when all facts needed
by the shader and backend are explicit:

- valid mesh/geometry table ranges;
- non-zero index and instance counts;
- material pipeline signature resolved for the current opaque RenderPathNode;
- typed draw record index when `SceneDraws` or `SceneObjects` is consumed;
- typed source material reference when source-local material data is consumed;
- valid material storage and source-local material index;
- final shader reflection from the material-source variant.

The current implementation names `RenderWorkKind`, `kind`, `RasterDraw`, and
`RasterBatch` are old submission DTO details. They are not requirement concepts.
The opaque geometry route should move to explicit types such as
`OpaqueGeometryDrawCandidate` and `OpaqueIndirectBatch`, or equivalent names.
After the batch compiler accepts an opaque geometry candidate, the backend
submits it through indirect draw.

Missing data is a preparation error or an unsupported diagnostic. It is not a
reason to fall back to old direct/per-item submission.

### Batch Compatibility

Opaque batch compatibility is material-signature based inside one opaque
RenderPathNode.

The batch compiler is scoped to a concrete RenderPathNode/pass. That means pass
id, render state, attachment contract, target format, topology, and shader base
path are already fixed by the node context. They are diagnostics/context, not
per-draw batch split keys.

In the target architecture, bindless opaque geometry uses one global geometry
table/buffer model:

- vertex data exposed to the opaque pass is position-only for the fixed opaque
  shader input;
- topology for opaque geometry is triangle list;
- per-mesh variation is represented by draw/mesh table indices and ranges;
- debug line/wireframe rendering is not an opaque geometry batch variant. It
  belongs to debug/post-effect handling or a separate explicit pass.

Given that context, two opaque geometry candidates can be batched together
exactly when their material pipeline signature matches. In current terminology
this is the material source/type signature that selects the final shader variant
and therefore the pipeline for the current RenderPathNode.

The practical batch key for `073-e` is:

- material pipeline signature, derived from material type/source signature and
  final source-variant identity for the current RenderPathNode.

Everything else is either:

- fixed by the RenderPathNode;
- derived from the material pipeline signature for backend pipeline lookup;
- encoded as table index/range data inside the indirect command path;
- an invalid-input diagnostic;
- or old direct-submit state that must be deleted.

The compatibility signature must not include:

- `kind` / `RenderWorkKind`;
- `PipelineKey` as an independent key;
- `RenderPathNodeSignature` as a per-draw split key;
- material URI;
- material name;
- material parameter values;
- texture presence;
- per-material descriptor object identity;
- vertex layout as a per-draw PSO axis;
- object topology as a per-draw PSO axis;
- target/attachment data as a per-draw PSO axis;
- old `MaterialUBO` or `SceneGpuMaterialRecord` PBR payload identity;
- `techniques/...` shader URI.

Different material values or different texture slots stay in one batch when the
material pipeline signature matches. The draw command carries the table
index/range that selects the actual object/material/mesh data.

If current code cannot batch because two meshes still require different bound
vertex/index buffers, that is not a desired compatibility split. It is a missing
global geometry-table implementation detail for `073-e` to fix or fail-fast
with a diagnostic. It must not become a permanent batch key.

### Introduced Concept Audit

`073-e` uses the following concept boundaries:

| Concept | `073-e` status | Reason |
|---|---|---|
| material pipeline signature | Only opaque batch key | It selects the final source-variant shader/pipeline for the current opaque node. |
| RenderPathNode/pass context | Batch compiler scope, not a split key | Pass state, target, topology, and attachment contract are fixed before candidates are compared. |
| `PipelineKey` | Derived backend lookup value, not an independent batch key | Backend may need it to fetch/create the PSO, but batching should not compare a second identity that can drift from the material signature. |
| `RenderPathNodeSignature` | Current pipeline-cache implementation detail, not a per-draw batch key | The node signature belongs to the pass context. It must not be copied onto every draw as a reason to split. |
| draw/object/material/mesh indices and ranges | Indirect command payload and validation data | They select per-draw table records after batching; they do not create a new PSO. |
| descriptor resources | Global table/scene binding data only | Per-material descriptor identity is old bound-resource behavior and must not split same-signature materials. |
| vertex layout | Fixed opaque input contract or invalid input | Bindless opaque geometry exposes the fixed position-only input expected by the shader. |
| topology | Fixed opaque input contract or separate explicit pass | Opaque geometry is triangle list; debug line/wireframe rendering is not an opaque material batch variant. |
| target/attachment | RenderPathNode context | It affects the pass/pipeline environment, not compatibility between two draw candidates already inside that node. |
| `RenderWorkKind` / `kind` | Delete from opaque batching path | A runtime kind tag models the old union DTO. Opaque geometry should use typed candidate/batch structures instead. |

### Batch Compiler Result

`compileIndirectBatches()` must stop returning only
`std::vector<RenderIndirectBatch>`. The batching API must return an analysis
object such as:

```text
RenderIndirectBatchAnalysis
  batches
  diagnostics
  stats
```

The exact C++ names can differ, but the behavior must be explicit:

- every input item is either covered by one batch or has one diagnostic;
- no draw is silently skipped;
- diagnostics preserve item index, pass, object signature, material signature,
  material pipeline signature, RenderPathNode context, derived PipelineKey when
  available, and split/rejection reason;
- stats expose item count, batch count, indirect-capable draw count,
  unsupported draw count, and fallback-observed count.

Positive validation requires:

- `fallback-observed == 0`;
- no uncovered draw items;
- unsupported counts are zero except in tests that intentionally construct
  unsupported inputs.

### Split Reasons

Diagnostics use a constrained reason vocabulary so tests can assert exact
causes:

- `material-pipeline-signature-mismatch`;
- `missing-material-ref-index`;
- `missing-draw-record-index`;
- `invalid-material-ref-index`;
- `invalid-draw-record-index`;
- `missing-mesh-range`;
- `invalid-mesh-range`;
- `zero-index-count`;
- `zero-instance-count`;
- `global-geometry-table-missing`;
- `backend-indirect-unsupported`;
- `legacy-input-rejected`.

`073-e` must not report `descriptor-resource-mismatch` for per-material
texture/material differences. Descriptor resources may still be used to bind
global tables and scene-level resources, but they are not the per-material batch
key.

It also must not report permanent split reasons such as
`vertex-layout-mismatch`, `topology-mismatch`, `target-mismatch`, or
`geometry-buffer-mismatch` for opaque geometry. Those are either fixed by the
RenderPathNode/global geometry contract or invalid inputs that should fail
preparation.

### Vulkan Submission

The Vulkan realtime opaque geometry path consumes the batch analysis:

- empty queue returns normally;
- rejected analysis throws with the first diagnostic and exposes full stats in
  logs/validation output;
- successful analysis records indirect draw batches;
- old opaque direct/per-item geometry submission is deleted from the default
  material-source path.

If a low-level direct draw helper remains for unrelated debug, fullscreen, or
test-only paths, it must be outside the opaque material-source geometry route
and must be named/audited as non-default. It cannot be a fallback success path
for `073-e`.

The backend records enough observability for tests to prove indirect draw
was used. The mechanism can be a submission stats object, validation log, or
test hook, but it must not rely only on source grep.

## Helmet Smoke

`073-e` includes Helmet realtime smoke only.

The Helmet smoke proves:

- converted Helmet material-source scene loads;
- opaque RenderPathGraph path is used;
- final source-variant shader reflection is used;
- opaque draw items enter indirect batches;
- Vulkan backend submits indirect draw for the opaque pass;
- output is non-black;
- fallback-observed count is zero;
- no skipped draw is treated as success.

Helmet is intentionally chosen because it keeps this requirement focused on the
opaque indirect/batching path. BMW glass and transparent material coverage are
deferred to `073-f`.

## REQ-073-e Tests

### Batch Compiler Characterization

Add failing tests before implementation:

- current descriptor-resource equality split is rejected as old behavior for
  per-material resource differences;
- `RenderWorkKind` / `kind` is not part of the opaque batch contract;
- different vertex buffers or object topology cannot become permanent opaque
  batch split keys;
- zero index count, zero instance count, missing material ref, missing draw
  record, and invalid indices produce diagnostics instead of silent skips;
- every item is covered or diagnosed.

### Same Source Batching

Construct same-source material instances with different parameter values and
texture slots. They must share the compatible opaque batch when
the material pipeline signature matches.

### Split Diagnostics

Construct mismatches for material pipeline signature and invalid table/range
data. Assert the exact split or rejection reasons and preserved item identities.

### Backend Indirect Submission

Run the Vulkan submission path in a focused test or smoke harness and assert
that opaque geometry uses indirect batch submission rather than direct/per-item
draw submission.

### Helmet Smoke

Run low-resolution Helmet realtime smoke and assert:

- non-black output;
- material source / batch / draw / pipeline stats are present;
- indirect-capable draw count matches the expected opaque draw count;
- fallback-observed count is zero.

## REQ-073-f Handoff

`073-f` becomes the transparent BMW follow-up.

It owns:

- transparent queue sorting;
- transparent batching rules;
- transparent RenderPathGraph pass / RenderPathNode contract;
- glass material contract and shader variant support required by BMW;
- BMW converter support for BMW materials that are not covered by Helmet;
- BMW realtime smoke.

Transparent batching is a general mechanism, not a BMW-only shortcut:

```text
opaque:
  batch size and state locality may drive ordering

transparent:
  depth order is primary
  sort back-to-front first
  only contiguous compatible transparent items may merge into an indirect batch
```

The transparent pass must be explicit in RenderPathGraph. It must not be
selected by material name or implicit alpha heuristics. Glass or other BMW
transparent materials must either be supported with explicit source contracts
and shader variants, or fail-fast with diagnostics. They must not fall back to
opaque approximation, debug material, or skipped draw success.

`073-f` does not reopen `073-e` opaque batching architecture. It consumes the
batch compiler and backend indirect submission model from `073-e` and extends
it with transparent ordering constraints.

## Acceptance

`REQ-073-e` is ready for implementation planning when this spec is accepted and
the active requirement is aligned to:

- opaque-only batching and indirect draw;
- Helmet-only smoke;
- no BMW/glass/transparent scope in `073-e`;
- BMW/glass/transparent sorting moved to `073-f`.

`REQ-073-e` is implementation-complete only when:

- batch compiler tests pass;
- invalid-index and no-silent-skip diagnostics pass;
- descriptor equality no longer splits same-source per-material differences;
- backend indirect submission is proven;
- Helmet realtime smoke passes on the new path;
- `073-f` remains scoped to transparent/BMW follow-up.
