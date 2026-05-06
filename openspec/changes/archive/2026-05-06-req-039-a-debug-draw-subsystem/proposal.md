## Why

LXEngine currently has no high-level way to draw simple world-space debug primitives. Common editor and renderer-debug workflows still require constructing full scene nodes, materials, and mesh updates by hand, which blocks the next Phase 1.5 editor work and makes transient visualization too expensive to use.

REQ-039-a exists to close that gap now with a minimal v1 path: one-line debug line and wireframe helpers, a dedicated editor-overlay visibility layer, and a single overlay pass that reuses the existing frame-graph and pipeline infrastructure.

## What Changes

- Add a new `DebugDraw` subsystem with static draw helpers for lines, triangles, wire spheres, wire circles, wire boxes, cones, arrows, axes, and camera frusta.
- Add built-in color constants and a layer-scope helper so callers can temporarily override the default editor-overlay visibility routing.
- Add per-frame accumulation and flush hooks so debug lines are collected during a frame and emitted through one overlay render path.
- Add a dedicated `Pass_DebugOverlay` pass constant, debug-line shaders, and renderer integration so the debug geometry renders after the main forward pass and before ImGui overlay work.
- Add editor-overlay visibility mask constants and camera defaults so editor cameras can see debug output while game cameras do not.
- Add focused integration tests for geometry emission counts, line-limit clipping, and visibility-mask behavior.

## Capabilities

### New Capabilities
- `debug-draw-subsystem`: One-line world-space debug primitive drawing with per-frame batching, overlay visibility routing, and editor-facing helper APIs.

### Modified Capabilities
- `frame-graph`: Add a dedicated debug overlay pass constant and queue/build integration for the new overlay render path.
- `renderer-backend-vulkan`: Render the debug overlay pass with a line-list pipeline, debug-line shaders, depth-test-on/depth-write-off state, and preload support.

## Impact

- Affected code spans `src/core/debug_draw/`, scene visibility masks, frame-graph pass constants, engine-loop frame hooks, Vulkan renderer/pipeline integration, shader assets, and new integration tests.
- `REQ-041-a` editor MVP becomes unblocked for frustum, light-arrow, selection, and picking-ray visualization.
- No breaking public API changes are expected outside the addition of new debug-draw and visibility-layer interfaces.
