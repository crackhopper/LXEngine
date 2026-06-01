# Vendored third-party dependencies

This directory holds all third-party dependencies consumed by `LX_Infra`. They
are vendored directly into the repository so that `cmake` configuration and
build never require network access or a system package manager as a single
source of truth.

Allowed shapes:

- **Vendored header** — single-header libraries dropped under `include/<lib>/…`
- **Vendored source tree** — full source tree built as a subdirectory target
- **Vendored pre-built package** — platform libraries + headers checked in
  alongside an SDK-style layout

Disallowed shapes:

- git submodules
- CMake `FetchContent`
- Online download at first configure or during the build
- System package manager as the only available source

## Dependencies

### `include/cgltf` — glTF 2.0 parser

- **Shape:** vendored single header + one implementation host cpp
- **Upstream:** https://github.com/jkuhlmann/cgltf
- **Version:** v1.15
- **License:** MIT (see license notice at the end of `cgltf.h`)
- **Consumers:** `src/infra/mesh_loader/gltf_mesh_loader.cpp` (real glTF parsing),
  `src/infra/mesh_loader/cgltf_impl.cpp` (holds the single
  `#define CGLTF_IMPLEMENTATION`)
- **Notes:** Introduced by REQ-011 to replace the previous stub `GLTFLoader`.
  Handles ASCII `.gltf` with external `.bin` and external image files as the
  primary path; `.glb` is optional.

### `include/stb` — image and utility single headers

- **Shape:** vendored single header
- **Upstream:** https://github.com/nothings/stb
- **License:** MIT / public domain (see each header)
- **Consumers:** `src/infra/texture_loader/texture_loader.cpp` (holds
  `#define STB_IMAGE_IMPLEMENTATION`),
  `src/infra/offline/offline_image_writer.cpp` (holds
  `#define STB_IMAGE_WRITE_IMPLEMENTATION`)

### `include/tinyexr` — OpenEXR image writer

- **Shape:** vendored headers from TinyEXR release branch
- **Upstream:** https://github.com/syoyo/tinyexr
- **License:** BSD-style TinyEXR/OpenEXR notices (see `tinyexr.h`)
- **Consumers:** `src/infra/offline/offline_image_writer.cpp` (holds
  `#define TINYEXR_IMPLEMENTATION`)
- **Notes:** Used only by the offline image writer so renderer core, scene
  compiler, and GPU packing code do not depend on EXR-specific headers.

### `include/tinyobjloader` — Wavefront OBJ parser

- **Shape:** vendored single header
- **Upstream:** https://github.com/tinyobjloader/tinyobjloader
- **License:** MIT
- **Consumers:** `src/infra/mesh_loader/obj_mesh_loader.cpp` (holds
  `#define TINYOBJLOADER_IMPLEMENTATION`)

### `imgui/` — Dear ImGui + SDL3/Vulkan backends

- **Shape:** vendored source tree built via `external/cmake/imgui/CMakeLists.txt`
- **Upstream:** https://github.com/ocornut/imgui
- **License:** MIT
- **Consumers:** `src/infra/gui/imgui_gui.cpp`, `src/infra/gui/debug_ui.cpp`,
  `src/backend/vulkan/vulkan_renderer.cpp` (via `infra::Gui`)

### `SDL3/` — platform / windowing / input

- **Shape:** vendored pre-built package (headers + platform libraries)
- **Upstream:** https://github.com/libsdl-org/SDL
- **License:** Zlib (see `SDL3/LICENSE.txt`)
- **Consumers:** `src/infra/window/sdl_window.cpp`,
  `src/infra/window/sdl3_input_state.cpp`

### `SPIRV-Cross/` — SPIR-V reflection

- **Shape:** vendored source tree
- **Upstream:** https://github.com/KhronosGroup/SPIRV-Cross
- **License:** Apache 2.0 / MIT
- **Consumers:** `src/infra/shader_compiler/*`

### `yaml-cpp/` — YAML parser

- **Shape:** vendored source tree
- **Upstream:** https://github.com/jbeder/yaml-cpp
- **License:** MIT
- **Consumers:** `src/infra/material_loader/generic_material_loader.cpp`

## Updating a dependency

1. Download the new revision from upstream.
2. Replace the matching files under this tree (commit them to the repo).
3. Update the entry in this README with the new version / commit.
4. Rebuild `LX_Infra` and run the affected integration tests.
