# Agent Guidance — Renderer Demo

This document provides context and rules for AI coding assistants working on this project.

## Project Overview

A Vulkan-based 3D renderer written in C++20, structured in three layers:

| Layer | Directory | Role |
|-------|-----------|------|
| **core** | `src/core/` | Platform-agnostic interfaces, math, resource types, scene graph |
| **infra** | `src/infra/` | Infrastructure implementations (window, mesh/texture loaders, shader compiler) |
| **backend** | `src/backend/` | Vulkan rendering backend |

Tests are in `src/test/` (integration tests per module, no unit test framework).

## Shell Environment

This project is cross-platform (Windows + Linux).

- **Linux**: Use standard bash commands
- **Windows**: Use PowerShell syntax (see `.cursor/rules/powershell-shell-guidance.md`)
- Detect the platform from context before generating shell commands

## Build System

- **CMake** (minimum 3.16), C++20
- Build generator: **Ninja** (Linux) / Visual Studio or Ninja (Windows)
- External dependencies managed via `find_library` with fallback to `FetchContent`
- Shader compilation: `shaders/CMakeLists.txt` compiles GLSL to SPIR-V via `glslc`

### Key CMake Variables

| Variable | Description |
|----------|-------------|
| `SHADERC_DIR` | Custom path to shaderc (Windows) |
| `SPIRV_CROSS_DIR` | Custom path to SPIRV-Cross (Windows) |
| `USE_SDL` / `USE_GLFW` | Window backend selection |

### Build Commands (Linux)

```bash
mkdir build && cd build
cmake .. -G Ninja
ninja test_shader_compiler   # shader compiler test (no GPU needed)
ninja BuildTest              # all integration tests
ninja Renderer               # main application
```

## C++ Coding Standards

**CRITICAL**: Read and follow `openspec/specs/cpp-style-guide/spec.md` before writing C++ code.

Key rules:

1. **No raw pointers** for object references — use `std::unique_ptr`, `std::shared_ptr`, or `T&`
2. **Constructor injection only** — no setter-based dependency injection
3. **GPU objects** returned via `std::unique_ptr<T>` from factory functions
4. **RAII everywhere** — no manual `new`/`delete`
5. **`enum class`** over unscoped enums, `std::optional` over sentinel values

## Architecture Specifications

Detailed specifications live in `openspec/specs/`. Read the relevant spec before modifying a subsystem:

| Spec | Path | Covers |
|------|------|--------|
| **C++ Style Guide** | `openspec/specs/cpp-style-guide/spec.md` | Ownership, smart pointers, RAII, type safety |
| **Vulkan Backend** | `openspec/specs/renderer-backend-vulkan/spec.md` | VulkanDevice, Buffer, Texture, Shader, Pipeline, Renderer, CommandBuffer |
| **String Interning** | `openspec/specs/string-interning/spec.md` | `GlobalStringTable`, `StringID`, thread-safe string-to-int mapping |
| **Shader Compilation** | `openspec/specs/shader-compilation/spec.md` | Runtime GLSL→SPIR-V via shaderc, variant macros |
| **Shader Reflection** | `openspec/specs/shader-reflection/spec.md` | SPIR-V reflection via SPIRV-Cross, ShaderResourceBinding |
| **Window System** | `openspec/specs/window-system/spec.md` | IWindow interface, SDL/GLFW backends |
| **GUI System** | `openspec/specs/gui-system/spec.md` | ImGui integration |
| **Texture Loading** | `openspec/specs/texture-loading/spec.md` | Image loading (stb_image) |
| **Mesh Loading** | `openspec/specs/mesh-loading/spec.md` | OBJ/GLTF mesh loading (tinyobjloader) |
| **Resource pipeline hash** | `openspec/specs/resource-pipeline-hash/spec.md` | `getPipelineHash()` on mesh, material state, shaders, skeleton; future `PipelineKey` |
| **Skeleton resource** | `openspec/specs/skeleton-resource/spec.md` | `Skeleton` in core resources, UBO accessors, removal of `IComponent` |

### Change History

Completed changes are archived in `openspec/changes/archive/`. Active changes are in `openspec/changes/`.

## Key Design Patterns

### Resource System

- `IRenderResource` (base) in `src/core/rhi/render_resource.hpp` — all GPU resources inherit from this
- `IShader` interface in `src/core/asset/shader.hpp` — shader with reflection bindings
- `ShaderCompiler` / `ShaderReflector` / `CompiledShader` in `src/infra/shader_compiler/` — runtime GLSL compilation + SPIR-V reflection

### String Interning

- `GlobalStringTable` + `StringID` in `src/core/utils/string_table.hpp`
- `StringID` replaces `std::string` as key in material property maps
- Supports implicit construction from `const char*` / `std::string`

### Material System

- `MaterialTemplate` defines passes with shader + render state
- `MaterialInstance` holds per-instance property overrides keyed by `StringID`
- Binding cache built from shader reflection data

## Design Documents

Current design-oriented docs live in `notes/subsystems/`. Read the relevant doc for architecture context:

| Document | Path | Summary |
|----------|------|---------|
| **FrameGraph** | `notes/subsystems/frame-graph.md` | Pass orchestration, queue building, scene-level resource merge, and pipeline preload collection |
| **Geometry** | `notes/subsystems/geometry.md` | Mesh, vertex layout, index topology, and how geometry contributes to pipeline identity |
| **MaterialSystem** | `notes/subsystems/material-system.md` | Material template/instance flow, reflection-driven UBO writes, and descriptor resource ownership |
| **PipelineCache** | `notes/subsystems/pipeline-cache.md` | Backend pipeline cache semantics for preload, lookup, and runtime miss handling |
| **PipelineIdentity** | `notes/subsystems/pipeline-identity.md` | `PipelineKey`, `PipelineBuildDesc`, render signatures, and structured identity composition |
| **Scene** | `notes/subsystems/scene.md` | Scene container model, `RenderingItem` assembly path, and scene-level descriptors |
| **ShaderSystem** | `notes/subsystems/shader-system.md` | Runtime GLSL compile/reflect/package flow and the `CompiledShader` contract |
| **Skeleton** | `notes/subsystems/skeleton.md` | Skeleton resource, `SkeletonUBO`, and the pipeline signature for skinned rendering |
| **StringInterning** | `notes/subsystems/string-interning.md` | `GlobalStringTable`, `StringID`, structured compose/decompose, and debug-string reconstruction |
| **VulkanBackend** | `notes/subsystems/vulkan-backend.md` | Vulkan renderer/device/resource manager/command buffer integration with core abstractions |

## Conventions

- Namespace: `LX_core` (core layer), `LX_infra` (infra layer)
- Header-only for small utilities; `.hpp` + `.cpp` split for modules
- Shaders in `shaders/glsl/` with `.vert` / `.frag` extensions
- Integration tests: one executable per module in `src/test/integration/`

## Codex Command Workflow

- Prefer `rg --files` for file discovery and `rg -n` for text search.
- Prefer `sed -n` for focused file reads and `git status` / `git diff` for worktree inspection.
- Prefer `mv` for rename-only refactors and `perl -0pi` only for mechanical bulk rewrites after file moves.
- Use `cmake` and `ninja` for build verification on Linux.
- Command pre-authorization is controlled by `/home/lx/.codex/rules/default.rules`, not by this file.
- If a command fails for permission reasons, first try an already approved prefix instead of assuming the tool is unavailable.
