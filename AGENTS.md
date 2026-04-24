# Agent Guidance

This file is the single source of truth for coding agents in this repository.

If you are a coding agent, load this file first. Other agent entry files such as `CLAUDE.md` and `.cursorrules` must only point here and must not maintain their own duplicated project memory.

## Mission

Work from current repository facts only. Prefer actual code, `openspec/specs/`, and current `notes/` over stale summaries.

## Project Snapshot

LXEngine is a Vulkan-based 3D renderer written in C++20 with three layers:

| Layer | Directory | Role |
|---|---|---|
| `core` | `src/core/` | Platform-agnostic interfaces, math, resource types, scene graph |
| `infra` | `src/infra/` | Windowing, mesh/texture loaders, shader compiler, other infrastructure |
| `backend` | `src/backend/` | Vulkan backend |

Important executable areas:

- `src/demos/scene_viewer/`: main interactive demo
- `src/test/`: integration tests
- `assets/`: runtime assets and test assets

## Read Order

Before modifying code:

1. Read this file.
2. Read `openspec/specs/cpp-style-guide/spec.md`.
3. Read the relevant subsystem spec in `openspec/specs/<capability>/spec.md`.
4. Read the matching design note in `notes/` when architecture context matters.

Before modifying docs:

1. Read this file.
2. Read `openspec/specs/notes-writing-style/spec.md`.
3. Read the current target note in `notes/`.

## Shell And Platform

This repo is cross-platform.

- Linux: use bash commands
- Windows: use PowerShell syntax
- PowerShell guidance: `.cursor/rules/powershell-shell-guidance.md`

Detect platform from context before proposing commands.

## Build And Test

Build system facts:

- CMake 3.16+
- C++20
- Ninja on Linux, Visual Studio or Ninja on Windows
- Shader compilation from `assets/shaders/` via `glslc`

Key CMake variables:

| Variable | Meaning |
|---|---|
| `SHADERC_DIR` | custom shaderc path on Windows |
| `SPIRV_CROSS_DIR` | custom SPIRV-Cross path on Windows |
| `USE_SDL` / `USE_GLFW` | window backend selection |

Linux build:

```bash
mkdir build && cd build
cmake .. -G Ninja
ninja demo_scene_viewer
```

Linux test:

```bash
ninja test_shader_compiler
ninja BuildTest
ctest --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --output-on-failure -L requires_video_device
```

Linux Vulkan notes:

- Windowed Vulkan tests need `libSDL3.so.0`.
- Headless Linux often reports `No available video device` without X11.
- Prefer `xvfb-run -a ./src/test/<test-binary>` for Vulkan and SDL smoke tests.
- Read `openspec/specs/renderer-backend-vulkan/spec.md` before changing Vulkan integration tests.

## Hard Rules

C++ rules:

- No raw pointers for object ownership or object references. Use `std::unique_ptr`, `std::shared_ptr`, or references.
- Constructor injection only. No setter-based dependency injection.
- GPU objects should come from factories as `std::unique_ptr<T>`.
- RAII everywhere.
- Prefer `enum class` and `std::optional`.

Repository rules:

- Use current repository facts only.
- Do not preserve dead commands, dead directories, or compatibility notes for removed workflows.
- If documentation and code disagree, trust code and current specs first, then repair docs.

## Specs Index

Core specs commonly needed by agents:

- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/shader-compilation/spec.md`
- `openspec/specs/shader-reflection/spec.md`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/material-asset-loader/spec.md`
- `openspec/specs/frame-graph/spec.md`
- `openspec/specs/pipeline-key/spec.md`
- `openspec/specs/pipeline-build-desc/spec.md`
- `openspec/specs/pipeline-cache/spec.md`
- `openspec/specs/render-signature/spec.md`
- `openspec/specs/string-interning/spec.md`
- `openspec/specs/skeleton-resource/spec.md`
- `openspec/specs/window-system/spec.md`
- `openspec/specs/gui-system/spec.md`
- `openspec/specs/mesh-loading/spec.md`
- `openspec/specs/texture-loading/spec.md`
- `openspec/specs/asset-directory-convention/spec.md`
- `openspec/specs/asset-path-helper/spec.md`
- `openspec/specs/test-build-execution/spec.md`
- `openspec/specs/notes-writing-style/spec.md`

Active implementation proposals live in `openspec/changes/`. Archived history lives in `openspec/changes/archive/`.

## Design And Notes Entry Points

Use these when you need architecture context:

- `notes/README.md`
- `notes/get-started.md`
- `notes/architecture.md`
- `notes/project-layout.md`
- `notes/subsystems/index.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/subsystems/frame-graph.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/shader-system.md`
- `notes/subsystems/string-interning.md`
- `notes/subsystems/skeleton.md`
- `notes/subsystems/vulkan-backend.md`
- `notes/concepts/scene/index.md`

## Current Command Workflow

Current command definitions live in:

- `.codex/commands/`
- `.codex/commands/opsx/`

Current common commands:

- `/draft-req`
- `/opsx:explore`
- `/opsx:propose`
- `/opsx:apply`
- `/opsx:archive`
- `/finish-req`
- `/update-notes`
- `/refresh-notes`
- `/sync-design-docs`

Typical path:

```text
idea / problem
  -> /draft-req      optional
  -> /opsx:propose
  -> /opsx:apply
  -> /opsx:archive
  -> /finish-req
  -> /update-notes
  -> /refresh-notes
  -> git commit
```

Not every task needs the whole chain:

- discussion only: `/opsx:explore`
- notes only: `/update-notes`
- design index only: `/sync-design-docs`

If a user asks to commit current work, either use normal git workflow or follow `.codex/skills/curate-and-commit/`.

## Notes Site Facts

- Source directory: `notes/`
- Navigation source: `notes/nav.yml`
- Site config generation: `scripts/_gen_notes_site.py`
- Local preview / restart: `scripts/serve-notes.sh`

Common commands:

```bash
scripts/serve-notes.sh
scripts/serve-notes.sh --foreground
scripts/serve-notes.sh --build
```

Do not reference `scripts/refresh-notes.sh`. It is not the current script.

## Search And Editing Preferences

- Prefer `rg --files` for discovery.
- Prefer `rg -n` for text search.
- Prefer `sed -n` for focused reads.
- Prefer `git status` and `git diff` for workspace inspection.
- Prefer `mv` for rename-only refactors.
- Use `cmake` and `ninja` for Linux verification.

## Entry File Contract

Agent-facing entry files must follow this contract:

- `AGENTS.md` is the only maintained memory file.
- `CLAUDE.md` is a pointer to `AGENTS.md`.
- `.cursorrules` is a pointer to `AGENTS.md`.
- New agent-specific entry files should also point here instead of copying repository memory.
