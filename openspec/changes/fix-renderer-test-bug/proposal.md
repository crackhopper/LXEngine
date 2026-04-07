## Why

The test_render_triangle.cpp test program fails to render a triangle correctly. The camera matrices are being updated but the rendering produces incorrect or no output, indicating a bug in either the camera matrix calculation, data upload to the GPU, or the draw call implementation.

## What Changes

- Fix camera matrix calculation in `Scene::camera->updateMatrices()`
- Fix `Renderer::uploadData()` to correctly transfer scene data to GPU
- Fix `Renderer::draw()` to properly issue draw commands
- Add proper error handling and validation to identify the root cause

## Capabilities

### New Capabilities
- `renderer-test-bug-fix`: Debug and fix the triangle rendering pipeline from camera setup through draw calls

### Modified Capabilities
<!-- No existing spec-level changes -->

## Impact

- **Affected code**: `src/test/test_render_triangle.cpp`, `core/gpu/renderer.hpp`, `core/gpu/renderer.cpp`
- **Backend**: Vulkan renderer implementation in `backend/vulkan/`
- **Resources**: Vertex buffer, index buffer, and mesh creation code