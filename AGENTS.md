# Agent Guidance

This file is the single source of truth for coding agents in this repository.

If you are a coding agent, load this file first. Other agent entry files such as `CLAUDE.md` and `.cursorrules` must only point here and must not maintain their own duplicated project memory.

## Mission

Work from current repository facts only. Prefer actual code, current `notes/`, and active Superpowers specs over stale summaries.

## Project Snapshot

LXEngine is a Vulkan-based 3D renderer written in C++20 with three layers:

| Layer | Directory | Role |
|---|---|---|
| `core` | `src/core/` | Platform-agnostic interfaces, math, resource types, scene graph |
| `infra` | `src/infra/` | Windowing, mesh/texture loaders, shader compiler, other infrastructure |
| `backend` | `src/backend/` | Vulkan backend |
| `new_common` | `src/new_common/` | C++20 module rewrite — platform, math, memory (new track, see Development Tracks) |
| `new_core` | `src/new_core/` | C++20 module rewrite — resource, game object (new track, see Development Tracks) |

Important executable areas:

- `src/demos/lxe_editor/`: main interactive demo
- `src/test/`: integration tests
- `assets/`: runtime assets and test assets

## Development Tracks

This repository runs **two parallel development tracks**. They are intentionally kept separate — code from one track must not be modified to accommodate the other unless explicitly requested.

### Regular track (agent-driven, minimal human review)

Directories: `src/core/`, `src/infra/`, `src/backend/`, `src/test/`, `src/demos/`

This is the original production codebase. Agents implement features and fix bugs here directly. Human review is sparse — the agent is trusted to produce correct, well-tested code.

### New track (refactored rewrite, high human review)

Directories: `src/new_common/`, `src/new_core/`, `src/test/new/`

This is a ground-up rewrite using C++20 named modules (`.cppm`). Every change is human-reviewed and manually adjusted. The code is deliberately dual-tracked alongside the regular code.

**Default rule:** Unless a spec or user request explicitly mentions the new track, target the **regular track only**. Do not modify, reference, or depend on `new` code for regular-track work.

When a spec or requirement explicitly targets the new track:

- Load the C++20 module migration skill first (`.qwen/skills/auto-skill-cpp20-module-migration/`)
- Expect detailed human review of every line
- Follow `new` track conventions: C++20 modules, partition imports (`import :Partition;`), no raw headers

## Read Order

Before modifying code:

1. Read this file.
2. Read the matching design note in `notes/` when architecture context matters.
3. Read any active Superpowers spec under `docs/superpowers/specs/` that covers the work.

Before modifying docs:

1. Read this file.
2. Read the current target note in `notes/`.
3. Follow the notes style in `.codex/skills/writing-notes/SKILL.md` when editing human-facing notes.

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
ninja lxe_editor
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
- Read the relevant subsystem notes before changing Vulkan integration tests.

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

## Superpowers Specs

Design and implementation planning now use the Superpowers workflow. Active design specs live under:

- `docs/superpowers/specs/`

Use Superpowers skills for brainstorming, planning, TDD, execution, and verification.

## Design And Notes Entry Points

Use these when you need architecture context:

- `notes/README.md`
- `notes/get-started.md`
- `notes/concepts-design/architecture.md`
- `notes/concepts-design/project-layout.md`
- `notes/concepts-design/build-info.md`
- `notes/source_analysis/index.md`
- `notes/concepts/material/index.md`
- `notes/concepts/material/file-to-instance.md`
- `notes/concepts/material/template-blueprint.md`
- `notes/concepts/material/shader.md`
- `notes/concepts/material/material-instance.md`
- `notes/concepts/material/template-and-pipeline.md`
- `notes/source_analysis/src/core/pipeline/pipeline_identity.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/concepts/material/pass-rendering-flow.md`
- `notes/subsystems/scene.md`
- `notes/source_analysis/src/core/asset/shader.md`
- `notes/source_analysis/src/core/utils/string_table.md`
- `notes/subsystems/skeleton.md`
- `notes/subsystems/vulkan-backend.md`
- `notes/concepts/scene/index.md`

## Current Command Workflow

Current command definitions live in:

- `.codex/commands/`

Current common commands:

- `/draft-req`
- `/finish-req`
- `/update-notes`
- `/refresh-notes`
- `/sync-design-docs`

Typical path:

```text
idea / problem
  -> /draft-req      optional
  -> Superpowers brainstorming/design
  -> Superpowers implementation plan
  -> implementation + verification
  -> /finish-req
  -> /update-notes
  -> /refresh-notes
  -> git commit
```

Requirements live under `notes/requirements/` and the filename prefix is the implementation order. One REQ file should cover one continuous implementation cycle; if a new requirement pushes part of an existing active REQ later, split that active REQ and prefer `NNN-a` / `NNN-b` suffixes before implementing instead of shifting every later active REQ.

Not every task needs the whole chain:

- discussion only: use Superpowers brainstorming
- notes only: `/update-notes`
- design index only: `/sync-design-docs`

If a user asks to commit current work, either use normal git workflow or follow `.codex/skills/curate-and-commit/`.

## Notes Site Facts

- Source directory: `notes/`
- Navigation source: `notes/nav.yml`
- Site config generation: `scripts/notes/generate_site_config.py`
- Local preview / restart: `scripts/notes/serve_site.sh`

Common commands:

```bash
scripts/notes/serve_site.sh
scripts/notes/serve_site.sh --foreground
scripts/notes/serve_site.sh --build
```

Do not reference `scripts/serve-notes.sh` or `scripts/refresh-notes.sh`. They are not current scripts.

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
