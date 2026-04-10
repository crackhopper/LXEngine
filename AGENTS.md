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

### Change History

Completed changes are archived in `openspec/changes/archive/`. Active changes are in `openspec/changes/`.

## Key Design Patterns

### Resource System

- `IRenderResource` (base) in `src/core/gpu/render_resource.hpp` — all GPU resources inherit from this
- `IShader` interface in `src/core/resources/shader.hpp` — shader with reflection bindings
- `ShaderCompiler` / `ShaderReflector` / `ShaderImpl` in `src/infra/shader_compiler/` — runtime GLSL compilation + SPIR-V reflection

### String Interning

- `GlobalStringTable` + `StringID` in `src/core/utils/string_table.hpp`
- `StringID` replaces `std::string` as key in material property maps
- Supports implicit construction from `const char*` / `std::string`

### Material System

- `MaterialTemplate` defines passes with shader + render state
- `MaterialInstance` holds per-instance property overrides keyed by `StringID`
- Binding cache built from shader reflection data

## Conventions

- Namespace: `LX_core` (core layer), `LX_infra` (infra layer)
- Header-only for small utilities; `.hpp` + `.cpp` split for modules
- Shaders in `shaders/glsl/` with `.vert` / `.frag` extensions
- Integration tests: one executable per module in `src/test/integration/`
