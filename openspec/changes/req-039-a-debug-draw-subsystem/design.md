## Context

LXEngine already has the low-level pieces needed to draw lines: line-list topology in core RHI, pipeline generation from validated renderable data, target-scoped frame-graph passes, and Vulkan pipeline support for topology-driven render state. What is missing is a high-level path that lets gameplay, editor, and renderer-debug code request transient world-space primitives without constructing dedicated scene content by hand.

REQ-039-a is cross-cutting because it adds a new core-facing subsystem, a new render pass identity, new shader assets, scene/camera visibility conventions, and engine-loop hooks that must line up with the existing frame lifecycle. The design therefore needs to keep integration shallow and avoid introducing a second rendering path outside the frame graph.

## Goals / Non-Goals

**Goals:**
- Provide one-line world-space debug draw helpers for common primitives.
- Batch all v1 debug output into one line-list renderable path per frame.
- Route debug output to editor cameras by default through a dedicated visibility layer.
- Reuse existing scene, frame-graph, pipeline-cache, and Vulkan draw infrastructure.
- Keep v1 implementation deterministic and testable with CPU-side geometry-count tests.

**Non-Goals:**
- Persistent or time-based debug draws.
- Mesh-as-debug wireframe generation.
- Variable line width, anti-aliasing, or text rendering.
- User-defined debug shaders or multiple debug pipelines.
- Reworking the broader RenderTarget model planned for REQ-042.

## Decisions

### 1. Model DebugDraw as a process-level facade with internal per-frame state

`DebugDraw` will expose static namespace-style helpers backed by one internal service instance. This matches the REQ's one-line-call usability goal and avoids threading a renderer/editor object through unrelated gameplay and test code.

Alternative considered: scene-owned debug systems. Rejected for v1 because it forces more plumbing into call sites and scene lifecycle before the editor MVP can use the feature.

### 2. Flush debug geometry through one transient scene node in a dedicated frame-graph pass

The subsystem will accumulate CPU-side `DebugLineVertex` data during the frame, then publish that data as one transient renderable that participates in a dedicated `Pass_DebugOverlay`. This keeps pipeline preload, queue build, and Vulkan draw logic inside the normal validated-renderable path instead of creating a parallel immediate-mode renderer.

Alternative considered: direct Vulkan command-buffer emission outside the scene/frame graph. Rejected because it would bypass pipeline-cache contracts, duplicate descriptor/state setup, and make test coverage weaker.

### 3. Use an editor-overlay visibility mask, not a second camera class

Debug output will default to `Layer_EditorOverlay`; editor cameras include that bit and gameplay cameras do not. This gives camera-level filtering without special-casing debug nodes in render-queue code and aligns with the existing scene picking / culling-mask model.

Alternative considered: hard-coding pass- or shader-level camera filtering. Rejected because visibility belongs to scene/camera policy, not shader identity.

### 4. Keep v1 batching single-pipeline and preloaded

The debug pass will use one unlit color shader pair and line-list topology. Pipeline state is fixed: depth test on, depth write off, alpha blend on. Preloading the pipeline during scene initialization avoids runtime compilation stalls during interactive editor use.

Alternative considered: generating per-primitive material variants. Rejected because all v1 primitives share the same rendering contract and variant churn adds no value.

### 5. Bound CPU accumulation with clip-and-warn behavior

The subsystem will cap accepted lines per frame and emit one warning when the cap is exceeded. Dropping newest lines is simpler than attempting dynamic growth or back-pressure and matches the debug-only nature of the system.

Alternative considered: unbounded growth. Rejected because debug helpers are easy to overuse accidentally and would create unstable frame-time and memory behavior.

## Risks / Trade-offs

- [Transient node integration may fight existing scene ownership assumptions] -> Build the debug renderable path to be explicit about lifetime and add focused tests around queue construction and frame reset.
- [Overlay pass ordering may drift from UI ordering] -> Register `Pass_DebugOverlay` explicitly after `Pass_Forward` and before ImGui draw execution in renderer flow.
- [Static facade can hide lifecycle bugs] -> Keep `beginFrame()` / `endFrame()` explicit and wire them in `EngineLoop`, with tests verifying reset behavior.
- [Debug geometry may compile as a normal material path with extra overhead] -> Accept the small cost in v1 to stay within current pipeline-cache and render-queue architecture; optimize only if profiling later proves necessary.
