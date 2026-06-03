# Unified Render Work Flow Design

## Goal

Make realtime and offline rendering share the same high-level flow:

```text
FrameGraph pass
  -> RenderWorkQueue
  -> RenderWorkItem
  -> RenderUploadPlan
  -> backend execute
```

The shared flow should make the renderer easier to reason about without forcing
realtime raster, offline compute tracing, and future hardware ray tracing to use
the same low-level execution model.

## Core Semantics

`RenderingItem` is renamed and redefined as `RenderWorkItem`.

`RenderWorkItem` means one pipeline-compatible GPU work submission. It is not a
scene object and is not limited to one mesh. A work item can represent:

- one raster indexed draw
- one raster batch
- one compute dispatch
- one ray tracing dispatch

`RenderQueue` is renamed to `RenderWorkQueue`.

`RenderWorkQueue` means the ordered work list for one frame-graph pass. A pass
may contain many work items when the pass needs multiple pipeline submissions.
The current realtime path still creates one raster draw item per renderable as
an implementation detail, but the type names no longer encode that limitation.

## Work Payloads

`RenderWorkItem` keeps the pipeline-neutral fields at the top level:

- render domain
- work kind
- pass name
- target description
- shader
- material
- pipeline key
- descriptor resources

Raster-only fields move into `RasterDrawWorkPayload`:

- vertex buffer
- index buffer
- per-draw data
- draw range
- instance count

The first implementation only needs `RasterDrawWorkPayload`; compute and ray
tracing payloads are added as typed extension points so offline can adopt the
same flow without pretending to be raster.

## Upload Flow

Both realtime and offline should expose explicit upload planning.

Realtime upload planning is dirty-aware and resource-oriented. It consumes a
`RenderWorkQueue` and produces the list of CPU `IGpuResource` objects that must
be synchronized before execution. The current Vulkan resource manager can still
decide whether each resource is dirty internally, but the caller flow becomes
explicit: build queue, build upload plan, upload plan, execute queue.

Offline upload planning is scene-packet-oriented. It consumes the same scene
resource table and offline work context, then produces scene SSBO payloads,
software BVH data, frame params, and output buffers for the compute pipeline.

The public flow should look similar even when the internal implementation is
specialized:

```cpp
auto queue = buildRenderWorkQueue(context);
auto uploadPlan = buildRenderUploadPlan(context, queue);
uploader.upload(uploadPlan);
executor.execute(queue, uploadPlan);
```

## FrameGraph and Offline

Offline rendering should eventually use frame-graph pass planning as realtime
does. A software path tracing MVP can still be a single `OfflineRayTrace` pass,
but the architecture must allow offline denoise, accumulation, debug-output,
hardware RT build/update, and postprocess passes.

Realtime and offline may use different pass graphs. The shared contract is that
both graphs produce `RenderWorkQueue` instances and upload plans.

## Material Domains

Scene instances should be able to select materials by render domain. Short term,
an instance may expose realtime and offline material slots. Long term, the data
model should support a domain-keyed material binding so `RealtimeForward`,
`RealtimeDeferred`, `OfflineSoftwareRT`, and `OfflineHardwareRT` can diverge
without shader macro compatibility hacks.

## First Implementation Scope

The first implementation is intentionally structural:

1. Rename `RenderingItem` to `RenderWorkItem`.
2. Rename `RenderQueue` to `RenderWorkQueue`.
3. Move raster-only fields into a raster payload while preserving current
   realtime behavior.
4. Add `RenderDomain`, `RenderWorkKind`, and `RenderUploadPlan` as shared core
   concepts.
5. Make realtime Vulkan code call an explicit upload-plan path before drawing.
6. Keep offline software compute output behavior unchanged, but align naming and
   planning interfaces where practical.

This scope does not require offline multi-pass frame graph execution yet. That
comes after the common work item and upload plan vocabulary is in place.

## Testing

The migration is behavior-preserving for existing realtime rendering. Tests
should cover:

- queue sorting and pipeline-desc deduplication still work after the rename
- raster work payload still feeds pipeline construction and indexed drawing
- realtime upload planning lists vertex, index, draw, material, skeleton,
  camera, light, and IBL resources from work items
- offline render MVP still matches realtime output in the existing comparison
  test

