## Why

The `graphics_backend` directory defines backend interfaces (Renderer, IRenderResource) and a Vulkan implementation, but most of the implementation is incomplete or missing entirely. Critical components like device management, buffer/texture/shader creation, command buffer management, and pipeline construction have empty implementations. This prevents the entire renderer framework from running.

## Project Structure

```
renderer/                          # Project root
├── CMakeLists.txt                 # Main build entry point
├── shaders/                       # Shader source files (GLSL)
│   ├── CMakeLists.txt             # Shader build: glslc → .spv files (CompileShaders target)
│   └── glsl/
│       ├── blinnphong_0.vert      # Vertex shader
│       └── blinnphong_0.frag      # Fragment shader
└── src/
    ├── core/                      # Core interfaces
    ├── infra/                     # Infrastructure (window, etc.)
    ├── graphics_backend/          # Vulkan backend implementation
    └── test/                      # Integration tests
```

## What Changes

1. **Complete all empty/incomplete Vulkan backend implementations** across 9+ files
2. **Fix compilation issues** including wrong include paths and constructor mismatches in existing code
3. **Add integration tests** in `src/test/` to progressively verify each module's completeness and functionality
4. **Implement the renderer lifecycle** (`shutdown`, `initScene`, `uploadData`, `draw`) without modifying core interfaces

## Capabilities

### New Capabilities

- `renderer-backend-vulkan`: Complete Vulkan backend implementation covering device initialization, resource management (buffers, textures, shaders), command buffer recording and management, render pass construction, framebuffer creation, and graphics pipeline setup
- `renderer-testing`: Progressive integration tests for each module to verify correctness

### Modified Capabilities

- None (all changes are new implementations within existing interface constraints)

## Impact

- `src/graphics_backend/vulkan/details/` - Empty/incomplete implementations to complete:

  | File | Current Status | Implementation Needed |
  |------|----------------|----------------------|
  | `vk_device.cpp` | Empty stub | Instance, physical device, logical device, memory management |
  | `vkr_buffer.cpp` | Empty stub | Buffer creation, staging, mapping, data upload |
  | `vkr_texture.cpp` | Empty stub | Image creation, memory, layout transitions, staging copy |
  | `vkr_shader.cpp` | Empty stub | SPIR-V loading, shader module creation |
  | `vkr_renderpass.cpp` | Empty stub | Render pass with color/depth attachments |
  | `vkr_framebuffer.cpp` | Empty stub | Framebuffer from attachments |
  | `vkc_cmdbuffer.cpp` | Empty stub | Command recording (begin, bind, draw) |
  | `vkc_cmdbuffer_manager.cpp` | Empty stub | Command pool, buffer management |
  | `vkp_blinnphong.cpp` | Empty stub | BlinnPhong pipeline specialization |
  | `vkr_swapchain.cpp` | Implemented | `createDepthResources()` is stub |
  | `vk_renderer.cpp` | Partial | `shutdown`, `initScene`, `uploadData`, `draw` are empty stubs |
  | `vk_resource_manager.cpp` | Partial | `initializeRenderPassAndPipeline()` missing |

- `src/test/` - New integration test files for each module
- `src/core/gpu/` - Core interfaces remain unchanged (per constraint)
