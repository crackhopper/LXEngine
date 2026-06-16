# REQ-073-e Opaque Indirect Batching Design

Date: 2026-06-14

## Superseded Architecture Note

This spec originally described the low-level implementation shape for realtime
opaque geometry batching. That low-level architecture has been superseded by
`REQ-073-e2` and the Task 8/9 render input compiler hard cut.

This file remains active only as a behavior guardrail for opaque realtime
geometry, diagnostics, and Helmet smoke coverage. Do not use this file as an
implementation plan for the removed public queue/batch model. New implementation
work must use the current render input compiler path:

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler raster policy
  -> typed RenderInput[] payloads
  -> RenderInputDesc[] facts
  -> Vulkan desc-backed execution
```

Typed draw payloads stay in `RenderDrawInput` / typed `RenderInput` records.
`RenderInputDesc` stores validation, pipeline, binding, resource dependency,
diagnostic, and stats facts, and points back to the pass-local typed input by
`inputIndex`. It must not own or carry typed draw data.

## Durable Behavior Requirements

`REQ-073-e` behavior still matters for realtime opaque material-source
geometry:

- RenderPathGraph pass `input` declares scene renderables and the accepted
  opaque material types.
- `FramePass` stores the pass/input contract and pass-local metadata.
- `RenderWorkCompiler::buildInputs()` creates typed raster inputs from scene
  renderables, object facts, mesh facts, material facts, and resource-table
  state.
- `RenderWorkCompiler::prepare()` accepts or rejects each input and emits
  desc facts for validation, pipeline build, binding, resource dependencies,
  diagnostics, and stats.
- Opaque raster policy may group compatible accepted inputs internally for
  locality and indirect-capable submission, but that grouping is not a public
  result type or a separate compiler hierarchy.
- Vulkan submission consumes typed inputs together with accepted desc facts and
  pipeline/upload data. It must not infer draw payloads from descs alone.

## Compatibility And Diagnostics

Opaque compatibility is derived from facts the current pass and compiler can
prove:

- object-data ABI / table compatibility;
- material type and final shader variant compatibility;
- mesh/geometry range validity;
- render state and pipeline build facts owned by the pass/input contract;
- backend support for the selected submission path.

Rejected inputs must produce explicit diagnostics instead of silently
disappearing. Diagnostics and metadata should expose:

- compiler input count;
- accepted input count;
- rejected input count;
- submitted draw count;
- fallback observed count;
- precise rejection reason when an input cannot be prepared.

The success path must report `fallbackObservedCount == 0`.

## Helmet Smoke Guardrail

Helmet realtime smoke remains the required end-to-end behavior check for this
REQ family:

- Helmet scene loads through the current material-source path.
- Opaque RenderPathGraph input is compiled into typed raster inputs.
- Accepted inputs produce desc facts and pipeline/upload data.
- Vulkan submits the accepted draw coverage.
- Output is non-black.
- Metadata uses `renderInputStats`, not legacy batch stats.
- Fallback observation remains zero.

## Relationship To Follow-Up Requirements

- `REQ-073-e2` owns the hard cut to `RenderWorkCompiler`, typed `RenderInput`,
  and `RenderInputDesc`.
- `REQ-073-j` extends the same compiler path for transparent sorting behavior.
- `REQ-074-h` extends the same compiler path for OfflineRT graph-driven compute.
- `REQ-074-e` consumes deterministic pipeline build desc extraction from
  `RenderInputDesc.pipelineBuildDesc`.

Future work must extend the current compiler and desc-fact model. It must not
recreate the removed public queue/batch architecture under new names.
