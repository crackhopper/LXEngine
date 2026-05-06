## 1. OpenSpec And Core Contracts

- [x] 1.1 Add the debug-draw module skeleton, public API surface, color helpers, and layer-scope contract.
- [x] 1.2 Add editor-overlay visibility mask constants and the `Pass_DebugOverlay` frame-graph pass identity.

## 2. Debug Geometry And Frame Lifecycle

- [x] 2.1 Implement per-frame debug line accumulation, primitive builders, and line-limit clip-and-warn behavior.
- [x] 2.2 Wire `DebugDraw::beginFrame()` / `endFrame()` into the engine loop and publish one transient debug renderable path per frame.

## 3. Renderer And Shader Integration

- [x] 3.1 Add debug-line GLSL shaders and any material/pipeline definitions needed for the overlay line pipeline.
- [x] 3.2 Register and draw the debug overlay pass through the frame graph and Vulkan renderer in forward-then-overlay order.

## 4. Verification

- [x] 4.1 Add integration tests for line counts, primitive generation counts, visibility-layer routing, and frame reset behavior.
- [x] 4.2 Build and run targeted debug-draw and rendering verification commands, then fix any regressions they expose.
