# REQ-073-e Opaque Indirect Batching Design

Date: 2026-06-14

## Decision

`REQ-073-e` is the hard landing for realtime opaque geometry batching and
backend indirect draw submission.

`REQ-073-d` owns the `techniques/...` to `render_paths/...` migration, legacy
material-source route deletion, and ordinary positive-test cleanup. `REQ-073-e`
starts from that hard-cut baseline and focuses only on opaque material-source
geometry batching, backend indirect draw, diagnostics, and Helmet smoke.

## Current Context

`REQ-073-c` established the current material-source shader variant boundary:

```text
PipelineKey = MaterialTypeVariant + RenderPathNodeSignature
```

That is a current pipeline-cache implementation fact, not the target opaque
batch key for `073-e`. `RenderPathNodeSignature` is node/pass context, not a
per-draw comparison field. `073-e` batches prepared draws by object data ABI
signature plus material type signature, then derives the backend pipeline lookup
from that batch identity and the current RenderPathNode context. The current
bindless path has one object data ABI value, but keeping it explicit prevents
future object table/shader ABI variants from being conflated with material
types.

`REQ-073-d` is executing the hard cut for the old material/source compatibility
surface. After `073-d`, positive realtime material-source paths must have access
to:

- `render_paths/...` shader URIs;
- RenderPathGraph / RenderPathNode pass ownership;
- final source-variant shader reflection;
- `SceneResourceTableUploadView` data that can resolve typed
  material/draw/object indices during batch preparation.

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

`073-e` replaces that transitional behavior with a first-class node-level batch
analysis and submission contract for opaque geometry. It also removes the current
`RenderWorkKind`/`kind` concept from the opaque geometry path. `kind` is an implementation
artifact of the old "one union-like RenderWorkItem for every possible
submission" model; it is not a rendering contract and must not survive as an
opaque batching concept.

## REQ-073-e Scope

`073-e` includes:

- opaque Forward/Deferred prepared draw candidate readiness for indirect draw;
- table-index/range based batch compatibility;
- a diagnostic batch compiler result rather than silent skips;
- backend indirect draw submission as the realtime opaque geometry default;
- deletion of old opaque geometry direct/per-item success paths that the new
  indirect path replaces;
- Helmet realtime smoke on the new opaque path.

`073-e` excludes:

- additional realtime smoke scenes beyond Helmet;
- non-opaque material/pass support;
- OfflineRT;
- package, BC7, and pipeline cache serialization.

## REQ-073-e Architecture

The positive opaque path is:

```text
RenderPathGraph geometry node filtering opaque material types
  -> node-scoped object ABI resolver + material/source variant resolver
  -> SceneResourceTableUploadView
  -> RenderWorkQueue builds RenderPathNodeContext + RenderPathNodeData
       from handle/ref-level RenderDrawInput
  -> RenderBatchPreparation stage resolves per-input final reflection,
     signatures, typed table indices, and PreparedRenderDrawCandidate
  -> RenderBatchCompiler
       batches or rejects with diagnostics
  -> Vulkan indirect draw submission
```

`RenderWorkQueue` remains the owner of per-node work. `073-e` changes the shape
of that work from the old union-like `RenderWorkItem` DTO to a node-scoped data
model that the queue can hand to a generic batch compiler:

```text
RenderPathNodeContext
  render path node/pass identity
  rendering mode and sort policy
  render state defaults
  target/attachment contract
  geometry contract
  object data ABI resolver
  material signature resolver
  global geometry table view
  backend indirect capability

RenderPathNodeData
  RenderDrawInput[]
    object handle or renderable object reference
    mesh handle or mesh reference
    material handle or material reference
    local primitive/submesh reference when present
    debug identity and sort source data

RenderBatchPreparation stage output
  PreparedRenderDrawCandidate[]
    typed object/draw indices
    typed mesh table range
    typed material ref/source-local material indices
    object data signature
    material type signature
    final shader reflection identity used for readiness
    indirect draw counts and offsets
    sort key when the node policy needs it
```

`target`, attachments, render-state defaults, and pass identity are context for
the node that is currently being compiled. They are not fields copied onto each
draw input for comparison. A draw input builder should only need object, mesh,
and bound material references plus debug/sort source data. Typed GPU table
indices and indirect command payload are produced later by batch preparation
from `SceneResourceTableUploadView`; they must not be hand-authored upstream or
copied forward from the old `RenderWorkItem` path.

`RenderBatchPreparation` names a queue-owned stage or helper, not a new public
hierarchy. Its purpose is to make data provenance explicit:

| Value | Producer | Consumer |
|---|---|---|
| `RenderPathNodeContext` | RenderPathGraph node selection plus renderer/backend state for the current pass | Preparation, compiler, and backend submission as node scope |
| `RenderDrawInput` | Scene/renderable traversal from object, mesh/submesh, and bound material references | Preparation only |
| `SceneResourceTableUploadView` | SceneResourceTable upload build from `REQ-073-b` table state | Preparation only |
| final shader reflection | Material-source variant resolution from `REQ-073-c` for an input material/source in this node | Material signature resolver and readiness validation |
| object data signature | Object/draw table ABI resolver for the current bindless object data contract | Prepared candidate and backend pipeline lookup |
| material type signature | Material signature resolver for the input material in the current node | Prepared candidate and backend pipeline lookup |
| typed object/draw/material/mesh indices and ranges | Preparation, by resolving `RenderDrawInput` through `SceneResourceTableUploadView` | Prepared candidate validation and indirect command generation |
| `PreparedRenderDrawCandidate` | Preparation output after all required table facts are explicit | Sort policy and batch merge |
| `RenderBatchAnalysis` | Batch compiler output | Sole positive input to Vulkan geometry submission and diagnostics/tests |

This table is the implementation guardrail: a field cannot be added to an input
structure unless that input stage is the stage that can actually know it.

The compiler itself should not be named or structured as an opaque-only class.
`073-e` implements the opaque material-type policy through the generic
`RenderPathNodeContext` / `RenderPathNodeData` / `RenderBatchCompiler` shape.

The queue-owned batch pipeline flow is:

```text
RenderPathNodeData
  -> resolve RenderDrawInput through SceneResourceTableUploadView
  -> emit PreparedRenderDrawCandidate or preparation diagnostic
  -> validate prepared candidate readiness
  -> apply RenderPathNodeContext sort policy
  -> merge contiguous prepared candidates with the same batch signature
  -> return RenderBatchAnalysis
```

For `073-e`, the node policy can sort/group prepared candidates for batch
locality because the accepted material types are opaque and depth order is not
the semantic constraint.

If the current implementation only has per-mesh `GpuResourceRef` vertex/index
buffers at this point, `073-e` must first register/resolve them into the global
geometry table or reject the draw input with `global-geometry-table-missing`
during preparation. It must not compare those resource identities to decide
batch compatibility.

### Prepared Draw Candidate Readiness

A prepared draw candidate for a RenderPathNode that accepts opaque material
types is indirect-ready only when all facts needed by the shader and backend
have been resolved by batch preparation:

- valid mesh/geometry table ranges;
- non-zero index and instance counts;
- object data signature resolved for the current bindless object/draw table ABI;
- material type signature resolved for the current RenderPathNode;
- typed draw/object indices produced from the input object reference when
  `SceneDraws` or `SceneObjects` is consumed;
- typed material ref and source-local material indices produced from the input
  material reference when source-local material data is consumed;
- valid material storage and source-local material index;
- final shader reflection from the material-source variant.

The typed draw/object/material/mesh indices are preparation outputs. They are
not fields that scene/renderable code is allowed to pre-fill on
`RenderDrawInput`. This prevents the new path from becoming a wrapper around
old `RenderWorkItem::raster.drawRecordIndex` / `materialRefIndex` fields.

The current implementation names `RenderWorkKind`, `kind`, `RasterDraw`, and
`RasterBatch` are old submission DTO details. They are not requirement concepts.
The geometry route should move to explicit node-level context/data plus draw
input and prepared candidate structures under `RenderWorkQueue`. It must not
introduce an opaque-only parallel hierarchy. After the batch compiler accepts a
prepared candidate, the backend submits it through indirect draw.

Missing data is a preparation error or an unsupported diagnostic. It is not a
reason to fall back to old direct/per-item submission.

### Batch Compatibility

Batch compatibility is object-data-signature plus material-type-signature based
inside one concrete RenderPathNode.

The batch compiler is scoped to the RenderPathNode/pass currently being
compiled. That node declares the shader, render state, source/target contract,
attachment contract, geometry contract, and material type filters. Those facts
are node context; they are not copied onto every prepared candidate as split
keys.

For `073-e`, the current node accepts opaque material types. Opaque is not a
special node class in the batching model. If a surface needs distinct shader or
material behavior, that distinction belongs in the material type identity, and
the RenderPathNode lists the material types it supports. `073-e` implements only
the opaque material-type side.

In the target architecture, geometry data for a geometry RenderPathNode comes
from one global geometry table/buffer model:

- vertex data exposed to the node uses the node's declared geometry contract;
- `073-e` geometry uses triangle-list opaque material types;
- per-mesh variation is represented by draw/mesh table indices and ranges;
- debug line/wireframe rendering is not a material batch variant. It belongs to
  debug/post-effect handling or a separate explicit pass.

Given that context, two prepared geometry candidates can be batched together
exactly when their object data signature and material type signature match.

The object data signature is not the old mesh-derived `objectSignature`. It is
the shader-visible object/draw table ABI signature: which object/draw records,
buffers, and accessors the selected shader expects. For the current bindless
path this value is intentionally singular, for example
`BindlessObjectData.v1`. It is still part of the batch/pipeline identity
so future static/skinned/meshlet/other object data ABI variants do not need
another identity redesign.

Material type signature is the material-side identity used for batching. At the
requirement level it is the normalized material type identity, such as
`standard-pbr-opaque`. If source ABI or contract differences really select a
different material shader contract, they are part of this material type
signature. Pass/node identity is not part of it. In the current code,
`materialTypeVariant` is the nearest implementation carrier, but `073-e` must
not let that carrier smuggle RenderPathNode/pass identity into the batch key.

The practical batch key for `073-e` is:

- object data signature, derived from the bindless object/draw table ABI;
- material type signature, derived from the normalized material type identity
  and any material source ABI facts that change the material shader contract.

Everything else is either:

- fixed by the RenderPathNode;
- derived from the object data signature, material type signature, and
  RenderPathNode context for backend pipeline lookup;
- encoded as table index/range data inside the indirect command path;
- an invalid-input diagnostic;
- or old direct-submit state that must be deleted.

The compatibility signature must not include:

- `kind` / `RenderWorkKind`;
- `PipelineKey` as an independent key;
- `RenderPathNodeSignature` as a per-draw split key;
- old mesh-derived `objectSignature`, which must be deleted from the production
  path rather than preserved as a renamed compatibility key;
- material instance identity such as URI or name;
- per-instance material data such as parameter values and texture/resource
  handles;
- vertex layout as a per-draw PSO axis;
- object topology as a per-draw PSO axis;
- target/attachment data as a per-draw PSO axis;
- old `MaterialUBO` or `SceneGpuMaterialRecord` PBR payload identity;
- `techniques/...` shader URI.

Different material values, texture slots, or resource handles stay in one batch
when the object data signature and material type signature match. The draw
command carries the table index/range that selects the actual
object/material/mesh data for the shader. The batching decision compares type
identity, not each material instance's stored values.

If current code cannot batch because two meshes still require different bound
vertex/index buffers, that is not a desired compatibility split. It is a missing
global geometry-table implementation detail for `073-e` to fix or fail-fast
with a diagnostic. It must not become a permanent batch key.

### Introduced Concept Audit

`073-e` uses the following concept boundaries:

| Concept | `073-e` status | Reason |
|---|---|---|
| object data signature | Batch key and pipeline identity axis | It represents the shader-visible bindless object/draw table ABI. Current value is singular; future ABI variants can split intentionally. |
| material type signature | Batch key and material-side pipeline identity axis | It represents the material type/source contract identity. |
| RenderPathNode/pass context | Batch compiler scope, not a split key | Pass state, target, topology, and attachment contract are fixed before prepared candidates are compared. |
| `PipelineKey` | Derived backend lookup value, not an independent batch key | Backend may need it to fetch/create the PSO, but batching should derive it from object data signature, material type signature, and node context instead of comparing a second identity that can drift. |
| `RenderPathNodeSignature` | Current pipeline-cache implementation detail, not a per-draw batch key | The node signature belongs to the pass context. It must not be copied onto every draw as a reason to split. |
| draw/object/material/mesh indices and ranges | Preparation output, indirect command payload, and validation data | They select per-draw table records after input resolution; they do not create a new PSO. |
| descriptor resources | Delete as a compatibility concept | `DescriptorResourceList` equality belongs only to old-path audits; it is not part of the requirement model. |
| vertex layout | RenderPathNode geometry contract or invalid input | Bindless geometry exposes the input expected by the current node's shader/geometry contract. |
| topology | RenderPathNode geometry contract or separate explicit pass | `073-e` opaque material geometry is triangle list; debug line/wireframe rendering is not a material batch variant. |
| target/attachment | RenderPathNode context | It affects the pass/pipeline environment, not compatibility between two prepared candidates already inside that node. |
| old mesh-derived `objectSignature` | Delete from realtime geometry batching | It is a superseded legacy key and must not remain beside the new object data signature under any renamed compatibility route. |
| `RenderWorkKind` / `kind` | Delete from opaque batching path | A runtime kind tag models the old union DTO. Opaque geometry should use typed candidate/batch structures instead. |

### Batch Compiler Result

`RenderWorkQueue::compileIndirectBatches()` should remain the queue-owned entry
point, but it must stop returning only `std::vector<RenderIndirectBatch>`. The
queue-owned batching API must return an analysis object such as:

```text
RenderBatchAnalysis
  batches
  diagnostics
  stats
```

The exact C++ names can differ, but the behavior must be explicit:

- every `RenderDrawInput` is either rejected during preparation with one
  diagnostic or produces a prepared candidate;
- every prepared candidate is either covered by one batch or has one diagnostic;
- no draw is silently skipped;
- diagnostics preserve input draw index, pass, material/source identity, object
  data signature, material type signature, RenderPathNode context, prepared
  mesh/draw/material index/range when available, derived PipelineKey when
  available, and split/rejection reason;
- stats expose input draw count, prepared candidate count, batch count,
  indirect-capable draw count, unsupported draw count, and
  fallback-observed count.

The analysis must be a real submission artifact, not only a diagnostic side
channel. Its `batches` carry the batch ordinal, pipeline/material type
signature, object data signature, indirect command range, and prepared draw
coverage needed by backend submission tests to prove which compiler output was
rendered.

Positive validation requires:

- `fallback-observed == 0`;
- no uncovered input draws or prepared candidates;
- unsupported counts are zero except in tests that intentionally construct
  unsupported inputs.

### Hard Cut And rg Audit

`073-e` is not allowed to leave a second successful realtime geometry batching
or submission model beside the `RenderPathNodeContext` / `RenderPathNodeData` /
`RenderBatchCompiler` path.

Implementation must delete or isolate all duplicate concepts that can still
submit realtime geometry successfully:

- old union-like `RenderWorkItem` geometry routing;
- `RenderWorkKind` / `kind` / `RasterDraw` / `RasterBatch` as geometry batch
  concepts;
- opaque-only parallel classes such as `OpaqueBatch*`, `OpaqueGeometry*`, or
  `OpaqueIndirect*`;
- descriptor-resource equality as a batch compatibility check;
- target, attachment, topology, vertex layout, or geometry buffer identity as
  per-draw batch split keys;
- direct/per-item geometry submission as a successful fallback after the new
  indirect path exists.

Any surviving helper for unrelated debug, fullscreen, compute, or negative-test
paths must be explicitly named outside realtime material-source geometry. It
cannot be reachable as a positive geometry fallback.

Implementation completion must include rg audits. The exact commands may be
split by file scope, but they must prove no ordinary production or positive-test
hit remains for the deleted geometry concepts:

```bash
rg -n "OpaqueBatch|OpaqueGeometry|OpaqueIndirect" src/core src/backend src/test
rg -n "RenderWorkKind|RasterDraw|RasterBatch|\\.kind\\b" src/core src/backend src/test
rg -n "DescriptorResourceList|sameDescriptorResources|descriptor-resource-mismatch" src/core src/backend src/test
rg -n "vertex-layout-mismatch|topology-mismatch|target-mismatch|geometry-buffer-mismatch" src/core src/backend src/test
rg -n "executeWorkItem|direct.*draw|per-item" src/backend src/core src/test
```

Allowed hits must be narrow and named in the completion report, such as negative
legacy audits or non-geometry compute/debug paths. Broad allowlists are not
acceptable.

### Split Reasons

Diagnostics use a constrained reason vocabulary so tests can assert exact
causes:

- `material-type-signature-mismatch`;
- `object-data-signature-mismatch`;
- `source-material-ref-unresolved`;
- `object-draw-record-unresolved`;
- `invalid-source-material-ref`;
- `invalid-draw-record`;
- `missing-mesh-range`;
- `invalid-mesh-range`;
- `zero-index-count`;
- `zero-instance-count`;
- `global-geometry-table-missing`;
- `backend-indirect-unsupported`;
- `legacy-input-rejected`.

`073-e` must not report `descriptor-resource-mismatch` for per-material
texture/material differences. `DescriptorResourceList` equality is an old-path
implementation detail and belongs only in hard-cut audits, not in the runtime
batch compatibility model.

It also must not report permanent split reasons such as
`vertex-layout-mismatch`, `topology-mismatch`, `target-mismatch`, or
`geometry-buffer-mismatch` for realtime geometry. Those are either fixed by the
RenderPathNode/global geometry contract or invalid inputs that should fail
preparation.

### Backend Batch Consumption

The Vulkan realtime geometry path consumes `RenderBatchAnalysis.batches` as the
only successful geometry draw source:

- empty queue returns normally;
- rejected analysis throws with the first diagnostic and exposes full stats in
  logs/validation output;
- successful analysis records indirect draw commands from the accepted batches;
- backend command recording must not re-read `RenderDrawInput`, old
  `RenderWorkItem`, or per-item raster payloads to submit material-source
  geometry after batch analysis succeeds;
- backend command recording must not run its own compatibility grouping that can
  diverge from `RenderBatchCompiler`;
- old opaque direct/per-item geometry submission is deleted from the default
  material-source path.

If a low-level direct draw helper remains for unrelated debug, fullscreen, or
test-only paths, it must be outside the opaque material-source geometry route
and must be named/audited as non-default. It cannot be a fallback success path
for `073-e`.

The backend records enough observability for tests to prove the submitted
drawcalls are the compiler-produced batches. The mechanism can be a submission
stats object, validation log, or test hook, but it must expose at least:

- compiler batch count consumed by the backend;
- submitted indirect batch count;
- submitted indirect draw count;
- first/last indirect command range or equivalent batch coverage identity.

Positive validation must assert those values match the `RenderBatchAnalysis`
returned by the queue for the rendered node. It must not rely only on source
grep or on "an indirect draw happened somewhere".

## Helmet Smoke

`073-e` includes Helmet realtime smoke only.

The Helmet smoke proves:

- converted Helmet material-source scene loads;
- opaque RenderPathGraph path is used;
- final source-variant shader reflection is used;
- prepared opaque draw candidates enter indirect batches;
- Vulkan backend submits indirect draw for the opaque pass by consuming the
  compiler-produced batch analysis;
- output is non-black;
- fallback-observed count is zero;
- no skipped draw is treated as success.

Helmet is intentionally chosen because it keeps this requirement focused on the
opaque indirect/batching path.

## REQ-073-e Tests

### Batch Compiler Characterization

Add failing tests before implementation:

- current descriptor-resource equality split is rejected as old behavior for
  per-material resource differences;
- `RenderWorkKind` / `kind` is not part of the opaque batch contract;
- current bindless object data signature is a single stable value and is part
  of the batch signature;
- old mesh-derived `objectSignature` is deleted from the production batching
  path and only allowed in named negative audits if needed;
- different vertex buffers or object topology cannot become permanent opaque
  batch split keys;
- zero index count, zero instance count, unresolved source material ref,
  unresolved draw record, and invalid prepared indices produce diagnostics
  instead of silent skips;
- every input draw is rejected during preparation or produces a prepared
  candidate, and every prepared candidate is covered or diagnosed.

### Same Source Batching

Construct same-source material instances with different parameter values and
texture slots. Their prepared draw candidates must share the compatible batch
when the object data signature and material type signature match.

### Split Diagnostics

Construct mismatches for object data signature, material type signature, and
invalid table/range data. Assert the exact split or rejection reasons and
preserved input/prepared identities.

### Backend Indirect Submission

Run the Vulkan submission path in a focused test or smoke harness and assert
that geometry uses the exact compiler-produced indirect batch analysis rather
than direct/per-item draw submission. The test must fail if the backend ignores
`RenderBatchAnalysis.batches`, submits old per-item raster payloads, or rebuilds
a separate grouping from raw draw inputs.

### Helmet Smoke

Run low-resolution Helmet realtime smoke and assert:

- non-black output;
- material source / batch / draw / pipeline stats are present;
- indirect-capable draw count matches the expected opaque draw count;
- fallback-observed count is zero.

## Acceptance

`REQ-073-e` is ready for implementation planning when this spec is accepted and
the active requirement is aligned to:

- opaque-only batching and indirect draw;
- Helmet-only smoke;
- no non-opaque material/pass or extra scene-smoke scope in `073-e`.

`REQ-073-e` is implementation-complete only when:

- batch compiler tests pass;
- invalid-index and no-silent-skip diagnostics pass;
- descriptor equality is removed from the positive batch compatibility model;
- hard-cut rg audits prove there is no second successful realtime geometry
  batching/submission path;
- backend indirect submission is proven;
- Helmet realtime smoke passes on the new path.
