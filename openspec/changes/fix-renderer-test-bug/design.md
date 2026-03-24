## Context

The test_render_triangle.cpp test program initializes a Vulkan renderer, creates a simple triangle mesh with vertex and index buffers, and attempts to render it in a loop. The rendering produces incorrect or no output despite camera matrices being updated each frame.

The rendering pipeline involves:
1. Vertex buffer creation with position, normal, UV, and bone data
2. Index buffer creation for a single triangle
3. Mesh creation combining vertex and index buffers
4. Scene creation with camera and mesh
5. Per-frame: camera matrix update, data upload, and draw call

## Goals / Non-Goals

**Goals:**
- Identify root cause of rendering failure
- Fix camera matrix calculation to produce correct view/projection matrices
- Fix GPU data upload to correctly transfer vertex/index buffer data
- Fix draw call implementation to properly invoke Vulkan draw commands
- Ensure triangle renders correctly in the test

**Non-Goals:**
- Full renderer refactoring or architecture changes
- Adding new rendering features beyond fixing the test
- Performance optimization

## Decisions

**1. Debug approach: Add validation at each pipeline stage**

Instead of guessing the bug location, add debug output/error checking at each stage:
- Validate camera matrix calculation (view matrix from position/target/up, projection from FOV/aspect/near/far)
- Validate vertex/index buffer data before and after upload
- Validate Vulkan command buffer state before draw

**2. Camera matrix fix: Verify lookAt implementation**

The Scene camera uses a lookAt-style camera. Common issues:
- View matrix: position, target, and up vectors not properly orthonormalized
- View matrix: matrix multiplication order incorrect (should be inverse of T*R)
- Projection matrix: FOV in degrees vs radians, aspect ratio calculation

**3. Upload mechanism: Verify buffer memory binding**

Vulkan requires:
- Creating buffer with appropriate size flags
- Allocating device memory and binding to buffer
- Using staging buffers for GPU-visible memory
- Proper memory barrier transitions

**4. Draw call: Verify vkCmdDraw parameters**

- Index count should be 3 (for triangle)
- First index should be 0
- Vertex buffer must be bound with vkCmdBindVertexBuffers
- Index buffer must be bound with vkCmdBindIndexBuffer

## Risks / Trade-offs

[Risk] Camera matrix bug → Mitigation: Add debug print of view/projection matrices, verify against known-good identity test
[Risk] Vulkan memory allocation failure → Mitigation: Add VK result checking on all memory operations
[Risk] Buffer not properly created → Mitigation: Verify buffer creation flags and size before binding

## Open Questions

- Is the Vulkan validation layer enabled? If not, errors may be silently swallowed.
- Are there existing unit tests for the camera matrix calculation that can be referenced?
- What is the expected behavior vs actual behavior? (black screen, distorted geometry, crash?)
