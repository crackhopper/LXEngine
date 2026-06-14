# REQ-073-e2 Render Work Compiler Single Path Hard Cut Design

Date: 2026-06-14

## Decision

`REQ-073-e2` is one hard-cut architecture correction. It does not split into a
compatibility phase and a deletion phase. The implementation may use ordered
internal checkpoints, but the completed source, assets, and positive tests must
not expose transitional public APIs or old vocabulary aliases.

The durable public model is:

```text
RenderPathGraph
  -> FrameGraph { FramePass[] }
  -> FrameGraph::compile()
       => CompiledFrameGraph
  -> RenderWorkCompiler
       => typed RenderInput[]
  -> prepare / validate
       => RenderInputDesc[]
  -> pipeline preload / upload / executor
       consumes RenderInput + RenderInputDesc
```

The final public vocabulary is `FramePass`, `CompiledFrameGraph`,
`RenderWorkCompiler`, `RenderInput`, `RenderDrawInput`, `RenderComputeInput`,
`RenderInputDesc`, and `PipelineBuildDesc`. The old `RenderWorkQueue`,
`RenderWorkItem`, `RenderWorkKind`, direct helper payloads, `RenderBatch*`,
`RenderIndirectBatch`, and batch analysis vocabulary are removed from
production code, assets, and ordinary positive tests.

## Current Context

Current repository facts show the dual path that this spec removes:

- `FramePass` still embeds `RenderWorkQueue queue`.
- `RenderWorkQueue` mixes newer `RenderDrawInput` / batch preparation state with
  old non-geometry `RenderWorkItem` dispatch items.
- `RenderWorkItem` and `RenderWorkKind` live in `src/core/scene/scene.hpp` and
  still carry direct raster and compute payloads.
- `PipelineBuildDesc::fromRenderWorkItem()` and
  `PipelineBuildDesc::fromRenderBatch()` are still backend-facing derivation
  paths.
- `VulkanResourceManager` still has `getOrCreatePipeline(RenderWorkItem)` and
  `getOrCreatePipeline(RenderBatch, context)`.
- `VulkanCommandBuffer` still has old work-item and render-batch execution
  entry points.
- RenderPathGraph assets currently use top-level `filters` and `geometry`;
  fullscreen passes are implied by `dispatch: fullscreen` instead of declaring
  their work input source.

This spec supersedes the older
`docs/superpowers/specs/2026-06-03-unified-render-work-flow-design.md` model
where `RenderWorkQueue` / `RenderWorkItem` were the intended shared flow.

## RenderPathGraph Input Schema

Every raster `RenderPassNode` must declare `input`. Old top-level `filters` and
`geometry` are removed from the schema and are not accepted as compatibility
fields. Parser diagnostics must fail fast when old fields are used.

Scene-renderable raster passes use:

```yaml
input:
  kind: scene-renderables
  object:
    renderClass: [surface.mesh]
  material:
    type: [matte, uber, metal, substrate, standard-pbr]
    required: true
  geometry:
    vertex: position-only
    topology: triangle-list
```

Semantics:

- `input.kind` answers where pass work comes from.
- `object.renderClass` filters object / renderable / mesh categories. Omitting
  it means the pass does not filter by object class.
- `material.type` filters material type. It replaces old `filters.bsdf`.
  Omitting it means the pass does not filter by material type.
- `material.required: true` rejects renderables without material.
- `material.required: false` allows material-less renderables. If
  `material.type` is also present, renderables with material must still match
  the listed type.
- `object.renderClass` and `material.type` compose with AND semantics.
- PBR material passes may rely on `material.type` without requiring an object
  render class.
- Debug/editor/helper mesh passes may rely on `object.renderClass` with
  `material.required: false`; their shader is an explicit no-material shader,
  not a hidden fallback material.

Fullscreen raster passes use:

```yaml
input:
  kind: fullscreen-triangle
```

Semantics:

- Only `stage: raster` plus `dispatch: fullscreen` may use
  `fullscreen-triangle`.
- The pass does not traverse scene renderables.
- The compiler generates one built-in fullscreen-triangle `RenderDrawInput`.
- `fullscreen-triangle` rejects `object`, `material`, and `geometry` subfields.

Compute input schema is reserved for `REQ-073-g`. This spec keeps the extension
point at `input.kind` but does not introduce OfflineRT-specific public compiler
types.

## FramePass And Compile Boundary

`FramePass` is the only per-pass owner. It stores:

- pass identity, target, reads, writes, stage, dispatch, shader URI, render
  state, rendering mode, attachments, and render path node signature;
- the parsed pass input contract from `RenderPassNode.input`;
- no `RenderWorkQueue`, backend pipeline, command buffer, or GPU ownership.

`FrameGraph::compile()` remains graph-only. It validates graph resource
vocabulary, producer / consumer DAG, imported/source/target/write-mode rules,
stable ordering, and source pass indices. It does not create `RenderInput`,
`RenderInputDesc`, pipeline keys, pipeline build descriptions, scene traversal,
offline storage, or compiler selection.

## RenderWorkCompiler And Input Family

`RenderWorkCompiler` is the only post-FrameGraph compiler entry. Selection is
based on `RenderDomain`, `FramePass.stage`, `FramePass.dispatch`,
`FramePass.input.kind`, graph contract, resolved shader payload, shader
reflection, and domain context. Selection must not use pass name, shader URI
substring, asset path substring, or OfflineRT special cases.

`RenderInput` is the typed execution payload family:

```text
RenderInput
  shared identity and diagnostic context only

RenderDrawInput
  raster execution payload
  source = SceneRenderable | FullscreenTriangle

RenderComputeInput
  compute execution payload
  dispatch group counts, local size, source/target/readback refs, domain payload
```

Payload ownership belongs to typed `RenderInput` instances, not to
`RenderInputDesc`. For raster, scene-renderable inputs carry object / mesh /
material refs when present, prepared draw data, and draw or indirect command
data. Fullscreen-triangle inputs carry the built-in fullscreen source identity.
For compute, dispatch dimensions and domain payload stay on `RenderComputeInput`.

`RenderInputDesc` is the prepared descriptor and validation result:

- accepted / rejected status;
- input index or typed input reference;
- `PipelineKey`;
- `PipelineBuildDesc`;
- shader URI, final variant, and reflection identity;
- descriptor binding plan;
- source / target resource dependency list;
- diagnostics;
- stats and coverage counters.

`RenderInputDesc` does not contain raster draw commands, indirect command
vectors, compute group payloads, or fullscreen geometry payload. The executor
reads payload from `RenderInput` and reads pipeline, binding, validation, and
diagnostic facts from `RenderInputDesc`.

Every input must produce either an accepted desc or a rejected desc with a
diagnostic. A failed input cannot be treated as empty success.

## Moving Existing Logic

The hard cut removes old types, not working behavior.

| Current logic | New owner |
|---|---|
| Scene traversal, visibility filtering, pass-local context assembly | `RenderWorkCompiler` scene-renderables input builder |
| Current `RenderDrawInput` handle/ref data | `RenderDrawInput` under the `RenderInput` family |
| Prepared draw candidate construction | raster compiler internals |
| Batch sorting, merge policy, indirect command coverage, diagnostics, stats | raster compiler and accepted/rejected `RenderInputDesc` stats |
| Direct helper fullscreen/post/debug/IBL work | typed raster inputs; fullscreen uses `input.kind: fullscreen-triangle` |
| Compute dispatch group/readback/resource mapping | `RenderComputeInput` and compute desc preparation |
| Pipeline derivation from work item or batch | desc preparation writes `RenderInputDesc.pipelineBuildDesc` |
| Queue-derived upload collection | upload plan consumes desc binding/resource facts plus input payload resources |

Internal helper structs may exist when file-local or parser-local, but no new
public `RenderInputAnalysis`, `RenderBatchResult`, `FramePassCompiler`,
`RenderSubmissionDesc`, `OpaqueBatch`, `OfflineCompiler`, or equivalent
second-path concepts are allowed.

## Backend Consumption

Backend pipeline creation consumes prepared descs:

```text
RenderInputDesc.pipelineBuildDesc
  -> VulkanResourceManager::getOrCreatePipeline(desc)
  -> PipelineCache::getOrCreatePipeline(PipelineBuildDesc, renderPass)
```

The backend must not derive pipeline facts from raw `RenderInput`. The compiler
preparation stage is responsible for pipeline key, shader stages, reflection
bindings, vertex layout, topology, render state, target, attachments, and push
constant layout.

Pipeline preload consumes accepted `RenderInputDesc` values, or a deduplicated
`std::vector<PipelineBuildDesc>` extracted only from those descs. The old
`FrameGraph::collectAllPipelineBuildDescs()` queue/item/batch traversal is
removed.

Upload planning consumes:

- descriptor binding plans and source/target resource refs from
  `RenderInputDesc`;
- concrete geometry or compute resources from typed `RenderInput`;
- no `RenderWorkQueue` or old item descriptor list.

Executor flow:

```text
for each accepted desc:
  input = inputs[desc.inputIndex]
  pipeline = resourceManager.getOrCreatePipeline(desc)
  bindPipeline(pipeline)
  bindResources(desc.bindingPlan)
  execute(input, desc)
```

Execution behavior:

- scene-renderable `RenderDrawInput` records indexed or indirect raster draws
  from the typed input payload;
- fullscreen-triangle `RenderDrawInput` records `vkCmdDraw(3, 1, 0, 0)` or an
  equivalent backend built-in path;
- `RenderComputeInput` records `vkCmdDispatch(...)`;
- rejected descs are not submitted and must remain visible in diagnostics and
  stats.

Batch-named runtime stats and metadata are replaced with input/desc names such
as `renderInputStats`, `compilerInputCount`, `acceptedInputCount`,
`rejectedInputCount`, `submittedDrawCount`, `submittedDispatchCount`, and
`fallbackObservedCount`.

## Tests And Audits

Implementation checkpoints remain inside one hard-cut spec:

1. Schema characterization:
   - raster pass missing `input` fails;
   - old top-level `filters` / `geometry` fails;
   - `input.kind: scene-renderables` succeeds;
   - `input.kind: fullscreen-triangle` succeeds only for raster fullscreen;
   - fullscreen input rejects object/material/geometry subfields;
   - `filters.bsdf` is migrated to `input.material.type`.
2. FramePass input contract:
   - parsed input contract reaches `RenderPassNode`, `FramePass`, and render
     path node signature;
   - `FrameGraph::compile()` tests prove no input/desc generation occurs.
3. Compiler and input family:
   - scene-renderables compiles to `RenderDrawInput[]`;
   - fullscreen-triangle compiles to exactly one built-in draw input;
   - every input yields accepted or rejected `RenderInputDesc`;
   - invalid material-less PBR input rejects instead of silently skipping.
4. Backend consumption:
   - pipeline build descs come from desc preparation;
   - Vulkan resource manager no longer accepts old item or batch APIs;
   - command buffer executes typed input plus desc;
   - upload plan comes from desc binding plans and typed input resources.
5. Old vocabulary deletion:
   - old production APIs, files, helpers, adapters, aliases, and positive tests
     are removed;
   - old path rejection is proven through new API behavior, not by keeping old
     type names in ordinary tests.
6. Smoke and docs:
   - Helmet realtime smoke goes through desc-backed path and produces non-black
     output;
   - metadata uses input/desc names;
   - rendering-pipeline concepts and active REQs describe the single path.

Final source/assets audit:

```bash
rg -n "RenderWorkItem|RenderWorkKind|DirectRasterWorkPayload|ComputeDispatchWorkPayload|DirectRasterPassPurpose|RenderWorkQueue|RenderBatch\\b|RenderBatchAnalysis|RenderBatchDiagnostic|RenderBatchStats|RenderBatchPipelineFacts|RenderBatchGeometryResources|RenderIndirectBatch|compileIndirectBatches|executeRenderBatch|fromRenderBatch|getOrCreatePipeline\\(.*RenderWorkItem|getOrCreatePipeline\\(.*RenderBatch|fromRenderWorkItem|Pass_OfflineRayTrace|OfflinePrimaryRayCompute" src assets

rg -n "RenderInputAnalysis|ComputeAnalysis|OpaqueBatch|OpaqueGeometry|OpaqueIndirect|Offline.*Compiler|Offline.*Work|compilerBatch|renderBatchStats|VulkanRealtimeRenderBatchStats|VulkanRenderBatchSubmissionStats" src assets
```

Both commands must produce no output for `src` and `assets`. Ordinary positive
tests must also migrate off old type tokens; legacy terms may remain only in
requirements and concept docs that explicitly describe history or audit rules.

Target verification commands:

```bash
cmake --build build --target test_render_path_graph_pass_contract
cmake --build build --target test_frame_graph
cmake --build build --target test_pipeline_build_info
cmake --build build --target test_bindless_indirect_contract
ctest --test-dir build --output-on-failure -R "render_path|frame_graph|pipeline|bindless"
python3 src/test/integration/test_helmet_standard_pbr_realtime_smoke.py
```

The completion report for implementation must include the negative tests or
audits added, final old-token audit output, exact build/test/smoke commands run,
and any explicitly named unsupported path with the owning follow-up REQ.

