## Context

The `graphics_backend` Vulkan implementation has a well-designed interface structure but incomplete implementations. Based on actual code exploration, 9 out of 12 core files have empty implementations or missing critical methods. This design addresses the implementation approach following the existing architecture without modifying core interfaces (`Renderer`, `IRenderResource`, `RenderItem`, `Scene`).

### Project Structure

```
renderer/                          # Project root
├── CMakeLists.txt                 # Main build entry point (line 40: add_subdirectory(shaders))
├── shaders/                       # Shader source files (GLSL)
│   ├── CMakeLists.txt             # Shader build: glslc → .spv files
│   └── glsl/
│       ├── blinnphong_0.vert      # Vertex shader
│       └── blinnphong_0.frag      # Fragment shader
└── src/
    ├── core/                      # Core interfaces
    ├── infra/                     # Infrastructure (window, etc.)
    ├── graphics_backend/          # Vulkan backend implementation
    │   └── vulkan/
    │       └── details/
    │           ├── vk_device.cpp  # EMPTY STUB
    │           ├── vkr_buffer.cpp  # EMPTY STUB
    │           ├── vkr_texture.cpp # EMPTY STUB
    │           ├── vkr_shader.cpp  # EMPTY STUB
    │           ├── vkr_renderpass.cpp   # EMPTY STUB
    │           ├── vkr_framebuffer.cpp  # EMPTY STUB
    │           ├── vkc_cmdbuffer.cpp    # EMPTY STUB
    │           ├── vkc_cmdbuffer_manager.cpp  # EMPTY STUB
    │           ├── vkp_blinnphong.cpp   # EMPTY STUB
    │           ├── vkr_swapchain.cpp    # Partial (depth resources missing)
    │           ├── vk_renderer.cpp      # Partial (lifecycle methods empty)
    │           └── vk_resource_manager.cpp # Partial (missing method impl)
    └── test/                      # Integration tests
```

### Build System

- **Main CMakeLists.txt** (line 40): `add_subdirectory(shaders)` includes shader build
- **Shader CMakeLists.txt**: Uses `glslc` (from Vulkan SDK) to compile `.vert`/`.frag` → `.spv`
- **Shader naming convention**: Code references shader name `blinnphong_0` (→ `shaders/glsl/blinnphong_0.vert.spv`)
- **Build target**: `CompileShaders` is a dependency of the main executable

### Current State (from exploration)
- **Empty stubs** (9 files): `vk_device.cpp`, `vkr_buffer.cpp`, `vkr_texture.cpp`, `vkr_shader.cpp`, `vkr_renderpass.cpp`, `vkr_framebuffer.cpp`, `vkc_cmdbuffer.cpp`, `vkc_cmdbuffer_manager.cpp`, `vkp_blinnphong.cpp`
- **Partial implementations** (3 files):
  - `vk_renderer.cpp`: Core methods (`shutdown`, `initScene`, `uploadData`, `draw`) are empty stubs
  - `vk_resource_manager.cpp`: `initializeRenderPassAndPipeline()` is declared but not implemented
  - `vkr_swapchain.cpp`: `createDepthResources()` is an empty stub
- **Core interfaces**: Well-defined in `core/gpu/` (Renderer, IRenderResource, RenderItem, Scene)

### Constraints
- **Must not modify** core interfaces in `src/core/gpu/`
- **Must not modify** data structures like `RenderItem`, `Scene`, `Mesh`, `Texture`
- **Must follow** the existing Vulkan backend architecture pattern

## Goals / Non-Goals

**Goals:**
- Complete all empty/incomplete Vulkan backend implementations to achieve a runnable renderer
- Fix all compilation issues (include paths, constructor mismatches)
- Add progressive integration tests to verify each module
- Implement in dependency order (device before resources, resources before commands, etc.)

**Non-Goals:**
- Modifying core interfaces or data structures
- Adding new interface abstractions beyond what exists
- Performance optimization (deferred to future work)
- Multi-threaded rendering (deferred to future work)

## Decisions

### Decision 1: Dependency-Ordered Implementation

Implement in strict dependency order:
1. VulkanDevice first - foundation for all other Vulkan objects
2. Resources (Buffer, Texture, Shader) - depend on device
3. RenderPass, Framebuffer, Swapchain depth - depend on device and resources
4. Command Buffers - depend on device and pipeline
5. Pipeline - depends on device, resources, and render pass
6. Renderer lifecycle - depends on all above

**Rationale**: Each component depends on previous ones. Cannot build pipeline without device and shaders.

### Decision 2: Test-First Per Module

For each module being implemented, create a corresponding integration test before implementing.

**Rationale**: Ensures each module works independently before integration. Provides regression protection during later phases.

**Alternatives considered**:
- Test-after: Risk of incomplete testing due to time pressure
- Big-bang test at end: Hard to isolate failures

### Decision 3: Resource Handle as Address

Use `IRenderResource*` (address) as the key for resource mapping.

**Rationale**: Already used by `getResourceHandle()` which returns `(void*)this`. Simple, no hash needed, lifetime tied to shared_ptr.

**Alternatives considered**:
- UUID-based: Adds complexity, no benefit given unique addresses
- Name-based: Requires string management, potential collisions

### Decision 4: SPIR-V Shader Loading

Use filesystem-based shader loading with naming convention `shaders/glsl/{shaderName}.vert.spv` and `shaders/glsl/{shaderName}.frag.spv`.

**Rationale**: Matches existing shader files at `shaders/glsl/` and `Shader::getShaderName()` pattern in codebase. The `CompileShaders` CMake target produces .spv files from .vert/.frag sources.

**Alternatives considered**:
- Embedded shaders: Increases binary size, harder to iterate
- Runtime compilation: Not supported by Vulkan, adds complexity

### Decision 5: VulkanDevice as Foundation

Implement `VulkanDevice` first - it is the foundation that all other components depend on.

**Rationale**: All other Vulkan objects require a valid device. Device creation includes:
- Instance creation with required extensions
- Physical device enumeration and selection
- Logical device creation with graphics and present queues
- Memory allocator setup

## Risks / Trade-offs

[Risk] Vulkan API complexity → Mitigation: Follow existing code patterns, use helper functions for common operations

[Risk] Dependency on external SPIR-V files → Mitigation: Create placeholder shader files if missing, error message clearly indicates path

[Risk] Memory management (VkDeviceMemory) → Mitigation: Implement matching alloc/free pairs, track all allocations for cleanup on shutdown

[Risk] Thread safety not addressed → Mitigation: Single-threaded render loop for initial implementation, documented as future work

## Open Questions

1. **Window/GLFW initialization**: Confirm expected initialization sequence in `initialize(WindowPtr)`
2. **Error handling strategy**: Vulkan errors are typically fatal - confirm expected behavior (throw? log+continue?)
3. **Shader build**: Shader files exist at `shaders/glsl/`, but CMakeLists.txt build has not been verified
