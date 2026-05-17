# Active Requirement Parallel FrameGraph Design

Date: 2026-05-17

## Purpose

This design defines how we will complete the current active requirement queue
without pretending the requirements are independent. The active queue is ordered
by dependency:

- `REQ-042-a`: FrameGraph v1 resource / target / pass execution
- `REQ-042-b`: directional shadow map and depth-only pass
- `REQ-042-c`: cascaded shadow maps
- `REQ-043-a`: shadow-era tutorial support
- `REQ-043-b`: architecture concepts and Mermaid diagrams

`REQ-042-a` is the hard implementation front door. The other active
requirements can be explored in parallel, but their implementation depends on
the contracts produced by `REQ-042-a`.

## Current Facts

Current code has a thin `FrameGraph` that builds per-pass `RenderQueue` objects
from a `Scene`. `FramePass` is currently a tuple of `name`, `target`, and
`queue`. `RenderTarget` only carries `colorFormat`, `depthFormat`, and
`sampleCount`. There are no named frame-graph resources, no pass read/write
declarations, no compiled execution plan, and no backend attachment registry.

Vulkan rendering currently opens one swapchain render pass and draws every
FrameGraph pass inside it. That is not true multi-pass rendering. Pipeline
identity is also target-unaware: `PipelineKey` is composed from object and
material signatures only, so depth-only and forward targets can collide unless
target identity becomes part of the key contract.

## Implementation Strategy

We will implement the active queue in dependency order, while using agents and
worktrees where the work is genuinely parallel.

The first implementation cycle focuses on `REQ-042-a`. Inside that requirement,
we split work into narrow lanes:

| Lane | Scope | Writes |
|---|---|---|
| Core FrameGraph | target/resource model, pass reads/writes, compile validation, debug inspection | `src/core/frame_graph/*`, frame graph tests |
| Pipeline Identity | target signature, target-aware pipeline key/build desc, dedupe behavior | `src/core/pipeline/*`, `src/core/utils/string_table.*`, related scene item path, pipeline tests |
| Vulkan Execution | backend attachment registry, pass boundaries, barriers, offscreen resources, swapchain final pass | `src/backend/vulkan/*`, Vulkan tests |
| Future REQ Research | read-only preparation for `REQ-042-b/c/043-a/043-b` | notes/plans only unless later promoted |

The Core FrameGraph and Pipeline Identity lanes can start in parallel because
their file ownership is mostly distinct. Vulkan Execution starts with read-only
adaptation planning, then writes against the integrated core contracts once
those contracts are stable enough to compile.

## Core Design

`RenderTarget` will be replaced or wrapped by a richer `RenderTargetDesc` that
describes target shape without owning backend resources. It will include the
minimum fields needed for `REQ-042-a`: attachment role, color attachment
presence and format, depth attachment presence and format, sample count, extent
policy, layer count, and target role such as swapchain or offscreen.

FrameGraph resources will be named by stable identifiers such as
`shadow.depth`, `swapchain.color`, and `swapchain.depth`. A resource declaration
will include enough description for the backend to create or bind the matching
attachment, but it will not hold Vulkan handles.

`FramePass` will grow read/write declarations. A pass can read named resources
as sampled images and write named color/depth attachments. Declaration order is
the v1 execution order; `FrameGraph::compile()` validates that every read has a
previous pass or external provider, that writes have legal target descriptions,
and that errors include pass name, resource name, and reason.

## Pipeline Identity Design

Pipeline identity becomes target-aware. The preferred shape is:

```text
PipelineKey(ObjectRender(...), MaterialRender(...), TargetRender(...))
```

The existing code currently builds the key too early, inside scene-node
validation, before target context is available. The implementation should keep
validated object and material signatures available, then compose the final key
in the queue/frame-graph path where the pass target is known. `PipelineBuildDesc`
must preserve target information so the Vulkan backend can build a compatible
pipeline for depth-only, forward swapchain, and future MRT/HDR targets.

## Vulkan Execution Design

The backend will execute compiled passes sequentially instead of drawing every
pass inside one swapchain render pass. It will own a frame-graph attachment
registry that maps resource names to backend bindings: image, view, sampler when
sampled, framebuffer/render-pass or dynamic rendering data, extent, format,
aspect, and current layout.

The first backend implementation should cover:

- undefined or read layout to attachment write
- color attachment write to shader sampled read
- depth attachment write to shader sampled read
- final swapchain color/depth write to present

The ImGui overlay remains tied to the final swapchain pass. Earlier offscreen
passes must not start or end ImGui frames.

## Parallel Agent Plan

The implementation will use one integration worktree for `REQ-042-a`, plus
short-lived worker worktrees only when a lane can own a disjoint write set. The
coordinator integrates results and runs verification.

Worker ownership rules:

- Core workers do not edit Vulkan backend files.
- Pipeline workers do not edit Vulkan backend files except tests that explicitly
  verify cache identity.
- Vulkan workers wait for the core and pipeline contracts before changing draw
  execution.
- Research workers for later REQs stay read-only unless the coordinator promotes
  a specific task after its dependencies are implemented.

## Verification

Core verification includes frame graph, pipeline identity, pipeline build desc,
and pipeline cache tests. New coverage must include:

- successful color offscreen write followed by sampled read
- successful depth-only write followed by sampled read
- missing resource read error with pass/resource/reason
- duplicate or illegal resource write error with pass/resource/reason
- target-aware pipeline keys for depth-only, offscreen, and swapchain targets
- FrameGraph build-desc dedupe preserving different targets as distinct entries

Backend verification includes Vulkan texture, framebuffer, command buffer,
pipeline, resource manager, and a smoke path for sequential pass execution. When
available, windowed Vulkan tests should run under `xvfb-run -a`.

Docs verification for `REQ-043-a/b` happens after shadow and CSM are implemented,
so tutorials and architecture diagrams describe current code rather than future
intent.

## Scope Boundaries

This design does not implement shadow formulas, CSM split logic, HDR/Post,
G-Buffer, deferred rendering, task-based command recording, resource aliasing,
pass reordering, Web Editor, Engine CLI/MCP, AssetRegistry hot reload, or custom
extension registries.

Those topics stay outside `REQ-042-a`. They are only revisited when their active
requirements become unblocked by the completed FrameGraph v1 work.
