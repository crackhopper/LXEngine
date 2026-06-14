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

`REQ-073-c` already established the material-source shader variant boundary:

```text
PipelineKey = MaterialTypeVariant + RenderPathNodeSignature
```

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
analysis and submission contract.

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
  -> MaterialTypeVariant + RenderPathNodeSignature
  -> SceneResourceTableUploadView
  -> RenderWorkItem with typed material/draw/object/mesh indices
  -> OpaqueBatchCompiler
       batches or rejects with diagnostics
  -> Vulkan indirect draw submission
```

### Work Item Readiness

An opaque geometry draw candidate is indirect-ready only when all facts needed
by the shader and backend are explicit:

- valid mesh/geometry buffer or table ranges;
- non-zero index and instance counts;
- populated `MaterialTypeVariant`;
- populated `RenderPathNodeSignature`;
- `PipelineKey::build(MaterialTypeVariant, RenderPathNodeSignature)`;
- typed draw record index when `SceneDraws` or `SceneObjects` is consumed;
- typed source material reference when source-local material data is consumed;
- valid material storage and source-local material index;
- final shader reflection from the material-source variant.

The current implementation name `RasterDraw` is only a pre-batch DTO detail. It
is not a requirement concept and must not preserve direct draw submission as a
successful path. After the batch compiler accepts an opaque geometry candidate,
the backend submits it through indirect draw.

Missing data is a preparation error or an unsupported diagnostic. It is not a
reason to fall back to old direct/per-item submission.

### Batch Compatibility

Opaque batch compatibility is structural, not descriptor-object based.

The compatibility signature is effectively pipeline identity plus compatible
mesh/geometry data. It includes:

- pass id / RenderPathNode id;
- `MaterialTypeVariant`;
- `RenderPathNodeSignature`;
- `PipelineKey`;
- render target / attachment contract as already represented in the node
  signature;
- geometry buffer/table compatibility;
- material source signature as represented by the material type variant and
  source-local material storage;
- indexed indirect command layout;
- backend indirect draw capability.

The compatibility signature must not include:

- material URI;
- material name;
- material parameter values;
- texture presence;
- per-material descriptor object identity;
- old `MaterialUBO` or `SceneGpuMaterialRecord` PBR payload identity;
- `techniques/...` shader URI.

Different material values or different texture slots stay in one batch when the
source signature, material type variant, node signature, and geometry
compatibility match. The draw command carries the table index/range that selects
the actual object/material data.

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
  material type variant, RenderPathNodeSignature, PipelineKey when available,
  and split/rejection reason;
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

- `pipeline-key-mismatch`;
- `render-path-node-signature-mismatch`;
- `material-type-variant-mismatch`;
- `geometry-buffer-mismatch`;
- `draw-command-layout-mismatch`;
- `missing-material-ref-index`;
- `missing-draw-record-index`;
- `invalid-material-ref-index`;
- `invalid-draw-record-index`;
- `zero-index-count`;
- `zero-instance-count`;
- `backend-indirect-unsupported`;
- `legacy-input-rejected`.

`073-e` must not report `descriptor-resource-mismatch` for per-material
texture/material differences. Descriptor resources may still be used to bind
global tables and scene-level resources, but they are not the per-material batch
key.

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
- zero index count, zero instance count, missing material ref, missing draw
  record, and invalid indices produce diagnostics instead of silent skips;
- every item is covered or diagnosed.

### Same Source Batching

Construct same-source material instances with different parameter values and
texture slots. They must share the compatible opaque batch when
`MaterialTypeVariant`, `RenderPathNodeSignature`, and geometry compatibility
match.

### Split Diagnostics

Construct mismatches for material type variant, RenderPathNodeSignature,
pipeline key, and geometry buffers. Assert the exact split reasons and preserved
item identities.

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
