# LXE Editor Promotion Live Bindless Viewport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote `lxe_editor` into `src/editor`, hard-cut old editor ownership paths, and fix the Helmet live viewport black frame through the current bindless render flow.

**Architecture:** First move editor production code into a first-class `LX_Editor` library plus the existing `lxe_editor` executable target, with source audits proving `src/core/editor` and demo-owned editor production code are gone. Then add a small editor-owned render-view adapter that feeds explicit camera and visibility facts into a renderer-neutral live view API; Vulkan continues to compile, upload, bind, and execute render work in `core`/`backend`.

**Tech Stack:** C++20, CMake/Ninja, Vulkan backend, ImGui, yaml-cpp, Python `unittest`, existing lxe_editor HTTP/MCP command surface.

---

## Required Guardrails For Rendering Tasks

Use current repo facts only. Read the relevant active requirement, subsystem note,
and current code before changing scope. This task is not a rename-only task.

First identify a negative test, audit, diagnostic, or code fact that proves the
current legacy path, silent fallback, ignored field, placeholder resource, or
mixed renderer graph path can leak through. Then implement or specify the
smallest change that closes that path.

Hard constraints for this plan:

- Do not introduce a second public graph/contract system beside
  `RenderPathGraph`, `FramePass`, `RenderWorkCompiler`, `RenderInput`, and
  `RenderInputDesc`.
- Do not put render-work compilation, upload planning, pipeline resolution,
  bindless descriptor upload, frame scheduling, or draw execution in
  `src/editor`.
- In the live viewport path, legacy `MaterialUBO`, old per-draw descriptors,
  material tags, non-bindless fallback, fake mesh/material resources, and
  missing typed indices must fail through tests, audits, or diagnostics.
- Delete superseded editor integration paths in the same slice. If a path is
  retained only for diagnostics, it must assert rejection rather than preserve
  old behavior.
- Verification must include exact build/test commands and source audits.

## Current Facts To Preserve

- `src/core/rhi/renderer.hpp` exposes `Renderer::initScene`, `uploadData`, and
  `draw`. `EngineLoop::tickFrame()` calls update hook, then `uploadData()`, then
  `draw()`.
- `src/backend/vulkan/vulkan_realtime_renderer.cpp` already has the clean
  explicit entry point needed for live rendering:
  `RenderWorkBuildContext::RealtimeOptions{cameraResource, visibleMask}`.
- Realtime profile output already succeeds by constructing an explicit
  `CameraResource` and visible mask before `prepareRenderPassInputs(...)`.
- The failing live viewport still reaches `hdr.color` and `depth.main`, but both
  remain clear for the Helmet scene.
- The current realtime/bindless descriptor evidence is in
  `RenderInputDesc.bindingPlan.descriptors`: accepted scene draws should carry
  scene GPU bindings such as `SceneObjects`, `SceneDraws`, `SceneMaterials`,
  `SceneMaterialRefs`, `SceneSourceMaterialRecords`, and `SceneTextures`.
- `src/demos/lxe_editor` is no longer just a demo: it owns API service,
  project/session state, command registration, recording, scene runtime, and UI.
- `src/core/editor` contains editor app behavior and is currently compiled into
  `LX_Core` by the recursive glob in `src/core/CMakeLists.txt`.

## Target File Structure

Create:

- `src/editor/CMakeLists.txt`: builds `LX_Editor` library and `lxe_editor`
  executable target.
- `src/editor/render/editor_render_view.hpp`
- `src/editor/render/editor_render_view.cpp`
- `src/core/rhi/live_render_view.hpp`
- `src/test/integration/test_lxe_editor_source_boundary.cpp`
- `src/test/integration/test_lxe_editor_render_view.cpp`
- `tests/lxe_editor/test_live_viewport.py`

Move production editor sources:

- `src/core/editor/*` to `src/editor/commands`, `src/editor/panels`, and
  `src/editor/ui`.
- `src/demos/lxe_editor/*` to `src/editor/api`, `src/editor/app`,
  `src/editor/commands`, `src/editor/project`, `src/editor/runtime`, and
  `src/editor/ui`.
- `src/demos/lxe_editor/README.md` to `src/editor/README.md`.

Modify:

- `CMakeLists.txt`
- `src/demos/CMakeLists.txt`
- `src/core/rhi/renderer.hpp`
- `src/core/gpu/engine_loop.hpp`
- `src/core/gpu/engine_loop.cpp`
- `src/backend/vulkan/vulkan_renderer.hpp`
- `src/backend/vulkan/vulkan_renderer.cpp`
- `src/backend/vulkan/vulkan_realtime_renderer.hpp`
- `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- `src/backend/vulkan/vulkan_renderer_types.hpp`
- `src/editor/main.cpp`
- `src/editor/app/editor_session.hpp`
- `src/editor/app/editor_session.cpp`
- `src/editor/api/lxe_editor_api_service.*`
- `src/editor/api/lxe_editor_api_server.*`
- `src/editor/commands/render_debug_commands.cpp`
- `src/test/CMakeLists.txt`
- C++ integration tests that include `core/editor/...` or
  `demos/lxe_editor/...`
- `tests/lxe_editor/api_client.py`

Namespace policy for this implementation cycle:

- Preserve C++ namespaces while moving code unless a local file becomes clearer
  with a small namespace rename. The hard-cut boundary is source ownership,
  include path, CMake target ownership, and dependency direction.
- Do not add forwarding headers, namespace aliases for old include paths, or a
  duplicate `src/demos/lxe_editor` target.
- If a full namespace rename to `LX_editor` is desired after the hard cut and
  black-frame fix, do it as a separate mechanical cleanup plan.

---

### Task 1: Add Source Boundary Audit Red Test

**Files:**
- Create: `src/test/integration/test_lxe_editor_source_boundary.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add the failing audit test**

Create `src/test/integration/test_lxe_editor_source_boundary.cpp`:

```cpp
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

std::string readText(const std::filesystem::path &path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool fileContains(const std::filesystem::path &path,
                  const std::string &needle) {
  if (!std::filesystem::is_regular_file(path)) {
    return false;
  }
  return readText(path).find(needle) != std::string::npos;
}

std::vector<std::filesystem::path>
filesContaining(const std::filesystem::path &root,
                const std::vector<std::string> &needles) {
  std::vector<std::filesystem::path> hits;
  if (!std::filesystem::exists(root)) {
    return hits;
  }
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    const std::string ext = path.extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".txt" &&
        path.filename() != "CMakeLists.txt") {
      continue;
    }
    const std::string text = readText(path);
    for (const std::string &needle : needles) {
      if (text.find(needle) != std::string::npos) {
        hits.push_back(path);
        break;
      }
    }
  }
  return hits;
}

void expectNoHits(const std::vector<std::filesystem::path> &hits,
                  const std::string &label) {
  if (hits.empty()) {
    return;
  }
  std::cerr << "[FAIL] " << label << '\n';
  for (const auto &hit : hits) {
    std::cerr << "  " << hit.generic_string() << '\n';
  }
  ++g_failures;
}

} // namespace

int main() {
#ifndef LXE_SOURCE_DIR
#error "LXE_SOURCE_DIR must be defined for test_lxe_editor_source_boundary"
#endif

  const std::filesystem::path sourceRoot = LXE_SOURCE_DIR;
  const auto coreEditorDir = sourceRoot / "src" / "core" / "editor";
  const auto demoEditorDir = sourceRoot / "src" / "demos" / "lxe_editor";
  const auto editorDir = sourceRoot / "src" / "editor";
  const auto rootCMake = sourceRoot / "CMakeLists.txt";
  const auto demosCMake = sourceRoot / "src" / "demos" / "CMakeLists.txt";

  EXPECT(std::filesystem::is_directory(editorDir),
         "src/editor must exist after editor promotion");
  EXPECT(!std::filesystem::exists(coreEditorDir),
         "src/core/editor must not remain as production code");
  EXPECT(!std::filesystem::exists(demoEditorDir),
         "src/demos/lxe_editor must not remain as production code");
  EXPECT(fileContains(rootCMake, "add_subdirectory(src/editor)"),
         "root CMake must add src/editor");
  EXPECT(!fileContains(demosCMake, "lxe_editor"),
         "src/demos/CMakeLists.txt must not add lxe_editor");

  expectNoHits(filesContaining(sourceRoot / "src",
                               {"#include \"core/editor/",
                                "#include <core/editor/",
                                "#include \"demos/lxe_editor/",
                                "#include <demos/lxe_editor/"}),
               "production source must not include old editor paths");
  expectNoHits(filesContaining(sourceRoot / "src" / "core",
                               {"CommandBus", "EditorState",
                                "ConsolePanel", "InspectorPanel",
                                "SceneTreePanel", "ViewportOverlay",
                                "GizmoAdapter"}),
               "src/core must not mention editor application classes");

  if (g_failures != 0) {
    std::cerr << g_failures << " editor source boundary checks failed\n";
    return 1;
  }
  return 0;
}
```

- [ ] **Step 2: Register the audit test**

In `src/test/CMakeLists.txt`, add `test_lxe_editor_source_boundary` to
`TEST_INTEGRATION_EXE_LIST` after `test_lxe_editor_recording`.

Add this block near the other `LXE_SOURCE_DIR` definitions:

```cmake
if(TARGET test_lxe_editor_source_boundary)
  target_compile_definitions(test_lxe_editor_source_boundary
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()
```

- [ ] **Step 3: Run the red test**

Run:

```bash
cmake --build build --target test_lxe_editor_source_boundary
ctest --test-dir build --output-on-failure -R '^test_lxe_editor_source_boundary$'
```

Expected before migration: FAIL, reporting `src/core/editor`,
`src/demos/lxe_editor`, and old include paths.

Do not commit yet; Task 2 makes this test pass.

---

### Task 2: Promote Editor Sources To `src/editor` And Hard-Cut Old Build Paths

**Files:**
- Create: `src/editor/CMakeLists.txt`
- Move/delete: `src/core/editor/**`
- Move/delete: `src/demos/lxe_editor/**`
- Modify: `CMakeLists.txt`
- Modify: `src/demos/CMakeLists.txt`
- Modify: `src/test/CMakeLists.txt`
- Modify: `src/test/integration/test_071g_legacy_boundary_removal.cpp`
- Modify: `tests/lxe_editor/api_client.py`
- Modify: all includes in `src/editor`, `src/test/integration`, and
  `tests/lxe_editor` that reference old editor paths

- [ ] **Step 1: Move files with `git mv`**

Run these commands:

```bash
mkdir -p src/editor/{api,app,commands,panels,project,runtime,ui}
mkdir -p src/editor/commands/builtin
git mv src/core/editor/command_bus.* src/editor/commands/
git mv src/core/editor/commands/builtin_commands.* src/editor/commands/
git mv src/core/editor/console_input_controller.* src/editor/panels/
git mv src/core/editor/console_panel.* src/editor/panels/
git mv src/core/editor/editor_config.* src/editor/app/
git mv src/core/editor/editor_state.* src/editor/app/
git mv src/core/editor/gizmo_adapter.* src/editor/ui/
git mv src/core/editor/inspector_panel.* src/editor/panels/
git mv src/core/editor/scene_tree_panel.* src/editor/panels/
git mv src/core/editor/viewport_overlay.* src/editor/panels/
git mv src/demos/lxe_editor/api_token_state.* src/editor/api/
git mv src/demos/lxe_editor/lxe_editor_api_protocol.* src/editor/api/
git mv src/demos/lxe_editor/lxe_editor_api_server.* src/editor/api/
git mv src/demos/lxe_editor/lxe_editor_api_service.* src/editor/api/
git mv src/demos/lxe_editor/display_launch_options.* src/editor/app/
git mv src/demos/lxe_editor/editor_config_state.* src/editor/app/
git mv src/demos/lxe_editor/editor_data_state.* src/editor/app/
git mv src/demos/lxe_editor/editor_log_file.* src/editor/app/
git mv src/demos/lxe_editor/editor_scene_state.* src/editor/app/
git mv src/demos/lxe_editor/editor_session.* src/editor/app/
git mv src/demos/lxe_editor/runtime_state.* src/editor/app/
git mv src/demos/lxe_editor/lxe_editor_commands.* src/editor/commands/
git mv src/demos/lxe_editor/commands/* src/editor/commands/
git mv src/demos/lxe_editor/builtin_asset_catalog.* src/editor/project/
git mv src/demos/lxe_editor/project_catalog.* src/editor/project/
git mv src/demos/lxe_editor/project_document.* src/editor/project/
git mv src/demos/lxe_editor/project_session.* src/editor/project/
git mv src/demos/lxe_editor/realtime_render_profile.* src/editor/project/
git mv src/demos/lxe_editor/scene_builder.* src/editor/project/
git mv src/demos/lxe_editor/scene_document.hpp src/editor/project/
git mv src/demos/lxe_editor/camera_rig.* src/editor/runtime/
git mv src/demos/lxe_editor/editor_camera_state.hpp src/editor/runtime/
git mv src/demos/lxe_editor/scene_input_routing.* src/editor/runtime/
git mv src/demos/lxe_editor/scene_interaction_controller.* src/editor/runtime/
git mv src/demos/lxe_editor/scene_runtime.* src/editor/runtime/
git mv src/demos/lxe_editor/scene_view_rect.* src/editor/runtime/
git mv src/demos/lxe_editor/selection_camera_input.hpp src/editor/runtime/
git mv src/demos/lxe_editor/recording_controller.* src/editor/runtime/
git mv src/demos/lxe_editor/ui_overlay.* src/editor/ui/
git mv src/demos/lxe_editor/main.cpp src/editor/main.cpp
git mv src/demos/lxe_editor/README.md src/editor/README.md
rmdir src/core/editor/commands
rmdir src/core/editor
rmdir src/demos/lxe_editor/commands
rmdir src/demos/lxe_editor
```

- [ ] **Step 2: Rewrite include paths**

Run:

```bash
rg -l 'core/editor/' src/editor src/test/integration tests/lxe_editor \
  | xargs perl -0pi -e 's#core/editor/commands/builtin_commands.hpp#editor/commands/builtin_commands.hpp#g; s#core/editor/command_bus.hpp#editor/commands/command_bus.hpp#g; s#core/editor/console_input_controller.hpp#editor/panels/console_input_controller.hpp#g; s#core/editor/console_panel.hpp#editor/panels/console_panel.hpp#g; s#core/editor/editor_config.hpp#editor/app/editor_config.hpp#g; s#core/editor/editor_state.hpp#editor/app/editor_state.hpp#g; s#core/editor/gizmo_adapter.hpp#editor/ui/gizmo_adapter.hpp#g; s#core/editor/inspector_panel.hpp#editor/panels/inspector_panel.hpp#g; s#core/editor/scene_tree_panel.hpp#editor/panels/scene_tree_panel.hpp#g; s#core/editor/viewport_overlay.hpp#editor/panels/viewport_overlay.hpp#g'

rg -l 'demos/lxe_editor/' src/editor src/test/integration tests/lxe_editor \
  | xargs perl -0pi -e 's#demos/lxe_editor/api_token_state.hpp#editor/api/api_token_state.hpp#g; s#demos/lxe_editor/lxe_editor_api_protocol.hpp#editor/api/lxe_editor_api_protocol.hpp#g; s#demos/lxe_editor/lxe_editor_api_server.hpp#editor/api/lxe_editor_api_server.hpp#g; s#demos/lxe_editor/lxe_editor_api_service.hpp#editor/api/lxe_editor_api_service.hpp#g; s#demos/lxe_editor/display_launch_options.hpp#editor/app/display_launch_options.hpp#g; s#demos/lxe_editor/editor_config_state.hpp#editor/app/editor_config_state.hpp#g; s#demos/lxe_editor/editor_data_state.hpp#editor/app/editor_data_state.hpp#g; s#demos/lxe_editor/editor_log_file.hpp#editor/app/editor_log_file.hpp#g; s#demos/lxe_editor/editor_scene_state.hpp#editor/app/editor_scene_state.hpp#g; s#demos/lxe_editor/editor_session.hpp#editor/app/editor_session.hpp#g; s#demos/lxe_editor/runtime_state.hpp#editor/app/runtime_state.hpp#g; s#demos/lxe_editor/lxe_editor_commands.hpp#editor/commands/lxe_editor_commands.hpp#g; s#demos/lxe_editor/commands/#editor/commands/#g; s#demos/lxe_editor/builtin_asset_catalog.hpp#editor/project/builtin_asset_catalog.hpp#g; s#demos/lxe_editor/project_catalog.hpp#editor/project/project_catalog.hpp#g; s#demos/lxe_editor/project_document.hpp#editor/project/project_document.hpp#g; s#demos/lxe_editor/project_session.hpp#editor/project/project_session.hpp#g; s#demos/lxe_editor/realtime_render_profile.hpp#editor/project/realtime_render_profile.hpp#g; s#demos/lxe_editor/scene_builder.hpp#editor/project/scene_builder.hpp#g; s#demos/lxe_editor/scene_document.hpp#editor/project/scene_document.hpp#g; s#demos/lxe_editor/camera_rig.hpp#editor/runtime/camera_rig.hpp#g; s#demos/lxe_editor/editor_camera_state.hpp#editor/runtime/editor_camera_state.hpp#g; s#demos/lxe_editor/scene_input_routing.hpp#editor/runtime/scene_input_routing.hpp#g; s#demos/lxe_editor/scene_interaction_controller.hpp#editor/runtime/scene_interaction_controller.hpp#g; s#demos/lxe_editor/scene_runtime.hpp#editor/runtime/scene_runtime.hpp#g; s#demos/lxe_editor/scene_view_rect.hpp#editor/runtime/scene_view_rect.hpp#g; s#demos/lxe_editor/selection_camera_input.hpp#editor/runtime/selection_camera_input.hpp#g; s#demos/lxe_editor/recording_controller.hpp#editor/runtime/recording_controller.hpp#g; s#demos/lxe_editor/ui_overlay.hpp#editor/ui/ui_overlay.hpp#g'
```

- [ ] **Step 3: Add the editor library and executable CMake**

Create `src/editor/CMakeLists.txt`:

```cmake
set(LXE_EDITOR_LIB LX_Editor CACHE INTERNAL "LXE editor application library")

set(LXE_EDITOR_SOURCES
  api/api_token_state.cpp
  api/lxe_editor_api_protocol.cpp
  api/lxe_editor_api_server.cpp
  api/lxe_editor_api_service.cpp
  app/display_launch_options.cpp
  app/editor_config.cpp
  app/editor_config_state.cpp
  app/editor_data_state.cpp
  app/editor_log_file.cpp
  app/editor_scene_state.cpp
  app/editor_session.cpp
  app/editor_state.cpp
  app/runtime_state.cpp
  commands/builtin_commands.cpp
  commands/command_bus.cpp
  commands/display_commands.cpp
  commands/lxe_editor_command_helpers.cpp
  commands/lxe_editor_commands.cpp
  commands/project_commands.cpp
  commands/realtime_render_commands.cpp
  commands/recording_commands.cpp
  commands/render_debug_commands.cpp
  commands/scene_project_commands.cpp
  panels/console_input_controller.cpp
  panels/console_panel.cpp
  panels/inspector_panel.cpp
  panels/scene_tree_panel.cpp
  panels/viewport_overlay.cpp
  project/builtin_asset_catalog.cpp
  project/project_catalog.cpp
  project/project_document.cpp
  project/project_session.cpp
  project/realtime_render_profile.cpp
  project/scene_builder.cpp
  runtime/camera_rig.cpp
  runtime/recording_controller.cpp
  runtime/scene_input_routing.cpp
  runtime/scene_interaction_controller.cpp
  runtime/scene_runtime.cpp
  runtime/scene_view_rect.cpp
  ui/gizmo_adapter.cpp
  ui/ui_overlay.cpp
)

add_library(${LXE_EDITOR_LIB} ${LXE_EDITOR_SOURCES})

target_include_directories(${LXE_EDITOR_LIB}
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/infra/external/yaml-cpp/include
)

target_link_libraries(${LXE_EDITOR_LIB}
  PUBLIC
    ${CORE_LIB}
    ${INFRA_LIB}
    ${GRAPHICS_LIB}
    imgui
    yaml-cpp::yaml-cpp
)

if(WIN32)
  target_link_libraries(${LXE_EDITOR_LIB} PUBLIC ws2_32)
endif()

add_executable(lxe_editor main.cpp)
target_link_libraries(lxe_editor PRIVATE ${LXE_EDITOR_LIB})
add_dependencies(lxe_editor CompileShaders)
```

- [ ] **Step 4: Wire top-level CMake and hard-cut demo ownership**

In root `CMakeLists.txt`, add this after `add_subdirectory(src/backend)`:

```cmake
option(LX_BUILD_EDITOR "Build the lxe_editor application" ON)
if(LX_BUILD_EDITOR)
  add_subdirectory(src/editor)
endif()
```

Keep `LX_BUILD_DEMOS`, but remove editor ownership from `src/demos/CMakeLists.txt`:

```cmake
# REQ-019: entry point for demo executables. Each demo lives in its own
# subdirectory. Demos are not registered with CTest.

add_subdirectory(minimal_resize)
add_subdirectory(minimal_resize_baseline)
```

- [ ] **Step 5: Link editor tests against `LX_Editor`**

In `src/test/CMakeLists.txt`, delete `LXE_EDITOR_COMMAND_SOURCES` and all
`target_sources(...)` entries that point at `../demos/lxe_editor/...`.

Add this helper after the `foreach(TEST_EXE ...)` loop:

```cmake
set(LXE_EDITOR_TESTS
  test_command_bus
  test_command_bus_v2
  test_scene_tree_panel
  test_editor_multi_select
  test_inspector_panel
  test_viewport_overlay
  test_gizmo_adapter
  test_lxe_editor_display_config
  test_lxe_editor_log_file
  test_lxe_editor_layout
  test_lxe_editor_interaction
  test_realtime_render_profile_commands
  test_lxe_editor_api_service
  test_lxe_editor_api_server
  test_lxe_editor_memory_probe
  test_lxe_editor_recording
  test_project_document
  test_project_catalog
  test_project_session
  test_scene_runtime
)

foreach(LXE_EDITOR_TEST ${LXE_EDITOR_TESTS})
  if(TARGET ${LXE_EDITOR_TEST})
    target_link_libraries(${LXE_EDITOR_TEST} PRIVATE ${LXE_EDITOR_LIB})
    target_include_directories(${LXE_EDITOR_TEST} PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/src/infra/external/yaml-cpp/include
    )
  endif()
endforeach()
```

If a target is not present in `TEST_INTEGRATION_EXE_LIST`, leave it in the
helper list guarded by `if(TARGET ...)`; do not add it just to satisfy the
helper.

- [ ] **Step 6: Update the Python harness executable path**

In `tests/lxe_editor/api_client.py`, update `_resolve_executable(...)` candidates:

```python
    candidates.extend(
        [
            repo_root / "build" / "src" / "editor" / "lxe_editor",
            repo_root / "build" / "src" / "demos" / "lxe_editor" / "lxe_editor",
        ]
    )
```

Keep the old path only as a test discovery fallback for developers with stale
build trees; the source boundary audit still prevents production code from
living under `src/demos/lxe_editor`.

- [ ] **Step 7: Update the legacy boundary scan root**

In `src/test/integration/test_071g_legacy_boundary_removal.cpp`, replace the
production root:

```cpp
      {"src/demos/lxe_editor"},
```

with:

```cpp
      {"src/editor"},
```

This keeps the legacy-token audit pointed at the promoted editor source root and
removes the stale `src/demos/lxe_editor` mention from `src`.

- [ ] **Step 8: Configure, build, and run boundary/editor tests**

Run:

```bash
cmake --build build --target lxe_editor BuildTest
ctest --test-dir build --output-on-failure -R 'test_lxe_editor_source_boundary|test_command_bus|test_command_bus_v2|test_editor_multi_select|test_inspector_panel|test_scene_tree_panel|test_viewport_overlay|test_gizmo_adapter|test_lxe_editor_layout|test_lxe_editor_api_service|test_lxe_editor_api_server|test_lxe_editor_interaction|test_realtime_render_profile_commands'
```

Expected: all listed tests pass. If CMake cannot find `src/editor`, run:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target lxe_editor BuildTest
```

- [ ] **Step 9: Run hard-cut source audits**

Run:

```bash
rg -n "core/editor" src
rg -n "#include \"core/editor|#include <core/editor" src
rg -n "demos/lxe_editor|src/demos/lxe_editor" CMakeLists.txt src
rg -n "CommandBus|EditorState|ViewportOverlay|InspectorPanel|SceneTreePanel" src/core
```

Expected:

- first three commands have no production source hits;
- the final command has no hits in `src/core`;
- docs and finished requirement text may still mention old paths, but this audit
  intentionally searches `src` and root CMake only.

- [ ] **Step 10: Commit the promotion**

Run:

```bash
git add CMakeLists.txt src/demos/CMakeLists.txt src/editor src/test/CMakeLists.txt src/test/integration tests/lxe_editor/api_client.py
git add -u src/core/editor src/demos/lxe_editor
git commit -m "refactor: promote lxe editor to src editor"
```

---

### Task 3: Add Editor Render View Adapter With Non-GPU Tests

**Files:**
- Create: `src/editor/render/editor_render_view.hpp`
- Create: `src/editor/render/editor_render_view.cpp`
- Create: `src/test/integration/test_lxe_editor_render_view.cpp`
- Modify: `src/editor/CMakeLists.txt`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add the adapter test first**

Create `src/test/integration/test_lxe_editor_render_view.cpp`:

```cpp
#include "editor/app/editor_state.hpp"
#include "editor/render/editor_render_view.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <iostream>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

SceneNodeSharedPtr makeCameraNode(const char *nodeName, const char *pathName,
                                  VisibilityLayerMask mask) {
  auto node = SceneNode::create(nodeName);
  node->setName(pathName);
  auto &camera = node->addComponent<CameraComponent>();
  camera.setCullingMask(mask);
  camera.clearTarget();
  camera.setAspect(16.0f / 9.0f);
  camera.updateMatrices();
  return node;
}

struct Fixture final {
  SceneSharedPtr scene = Scene::create(nullptr);
  SceneNodeSharedPtr editorCamera =
      makeCameraNode("editor_camera", "editor_cam", Layer_All);
  SceneNodeSharedPtr gameCamera =
      makeCameraNode("game_camera", "game_cam",
                     Layer_All & ~Layer_EditorOverlay);
  EditorState editorState;

  Fixture() {
    scene->addCamera(editorCamera);
    scene->addCamera(gameCamera);
    editorState.setEditorCamera(editorCamera);
    editorState.setPreviewCamera(gameCamera);
  }
};

void testEditorViewUsesEditorCameraWhenPreviewOff() {
  Fixture fixture;
  fixture.editorState.setPreviewEnabled(false);
  (void)fixture.editorState.syncActiveCamera(*fixture.scene);

  const auto view = LX_editor::buildEditorRenderView(
      fixture.editorState, *fixture.scene, Vec2f{1280.0f, 720.0f});

  EXPECT(view.has_value(), "preview-off view should be available");
  EXPECT(view->cameraPath == "/editor_cam",
         "preview-off view should use editor camera");
  EXPECT(view->previewEnabled == false,
         "preview-off view should report preview disabled");
  EXPECT(view->viewportExtent.x == 1280.0f && view->viewportExtent.y == 720.0f,
         "view should preserve viewport extent");
  EXPECT((view->visibleMask & Layer_EditorOverlay) == 0,
         "scene draw mask should exclude editor overlay");
  EXPECT(view->cameraResource.active,
         "view should carry an active camera resource");
}

void testPreviewViewUsesGameCameraWithoutTargetMatchDependency() {
  Fixture fixture;
  fixture.editorState.setPreviewEnabled(true);
  (void)fixture.editorState.syncActiveCamera(*fixture.scene);

  RenderTargetDesc hdrTarget;
  hdrTarget.role = RenderTargetRole::Offscreen;
  hdrTarget.colorFormat = ImageFormat::RGBA16Float;
  hdrTarget.depthFormat = ImageFormat::D32Float;
  const RenderTarget liveHdrTarget(hdrTarget);
  const auto gameCamera =
      fixture.gameCamera->getComponent<CameraComponent>();
  EXPECT(gameCamera.has_value(), "game camera component must exist");
  EXPECT(!gameCamera->get().matchesTarget(liveHdrTarget),
         "test requires old target matching to reject this camera");

  const auto view = LX_editor::buildEditorRenderView(
      fixture.editorState, *fixture.scene, Vec2f{640.0f, 480.0f});

  EXPECT(view.has_value(), "preview view should be available");
  EXPECT(view->cameraPath == "/game_cam",
         "preview view should use game camera");
  EXPECT(view->previewEnabled == true,
         "preview view should report preview enabled");
  EXPECT(view->visibleMask != 0,
         "explicit render view should not be filtered out by target mismatch");
  EXPECT((view->visibleMask & Layer_EditorOverlay) == 0,
         "preview view should exclude editor overlay");
}

} // namespace

int main() {
  testEditorViewUsesEditorCameraWhenPreviewOff();
  testPreviewViewUsesGameCameraWithoutTargetMatchDependency();
  if (g_failures != 0) {
    std::cerr << g_failures << " lxe editor render view checks failed\n";
    return 1;
  }
  return 0;
}
```

Add `test_lxe_editor_render_view` to `TEST_INTEGRATION_EXE_LIST` after
`test_lxe_editor_source_boundary`, and add it to `LXE_EDITOR_TESTS`.

- [ ] **Step 2: Run the red adapter test**

Run:

```bash
cmake --build build --target test_lxe_editor_render_view
ctest --test-dir build --output-on-failure -R '^test_lxe_editor_render_view$'
```

Expected: build fails because `editor/render/editor_render_view.hpp` does not
exist.

- [ ] **Step 3: Implement `EditorRenderView`**

Create `src/editor/render/editor_render_view.hpp`:

```cpp
#pragma once

#include "core/math/vec.hpp"
#include "core/scene/scene.hpp"

#include <optional>
#include <string>

namespace LX_core {
class EditorState;
} // namespace LX_core

namespace LX_editor {

struct EditorRenderView final {
  std::string cameraPath;
  LX_core::CameraResource cameraResource;
  LX_core::VisibilityLayerMask visibleMask =
      LX_core::Layer_All & ~LX_core::Layer_EditorOverlay;
  LX_core::Vec2f viewportExtent{0.0f, 0.0f};
  bool previewEnabled = false;
  bool editorOverlayVisible = true;
};

[[nodiscard]] std::optional<EditorRenderView>
buildEditorRenderView(const LX_core::EditorState &editorState,
                      const LX_core::Scene &scene,
                      const LX_core::Vec2f &viewportExtent);

} // namespace LX_editor
```

Create `src/editor/render/editor_render_view.cpp`:

```cpp
#include "editor/render/editor_render_view.hpp"

#include "editor/app/editor_state.hpp"

#include "core/scene/components/camera_component.hpp"

namespace LX_editor {

std::optional<EditorRenderView>
buildEditorRenderView(const LX_core::EditorState &editorState,
                      const LX_core::Scene &scene,
                      const LX_core::Vec2f &viewportExtent) {
  const LX_core::SceneNodeSharedPtr cameraNode =
      editorState.resolveActiveCamera(scene);
  if (!cameraNode) {
    return std::nullopt;
  }

  auto cameraComponent =
      cameraNode->getComponent<LX_core::CameraComponent>();
  if (!cameraComponent.has_value()) {
    return std::nullopt;
  }

  const std::string cameraPath = cameraNode->getPath();
  LX_core::CameraResource cameraResource =
      LX_core::Scene::makeCameraResource(
          cameraComponent->get().getSnapshot(cameraPath));
  cameraResource.active = true;
  cameraResource.cullingMask = cameraComponent->get().getCullingMask();

  return EditorRenderView{
      .cameraPath = cameraPath,
      .cameraResource = cameraResource,
      .visibleMask = cameraComponent->get().getCullingMask() &
                     ~LX_core::Layer_EditorOverlay,
      .viewportExtent = viewportExtent,
      .previewEnabled = editorState.isPreviewEnabled(),
      .editorOverlayVisible = !editorState.isPreviewEnabled(),
  };
}

} // namespace LX_editor
```

Add `render/editor_render_view.cpp` to `LXE_EDITOR_SOURCES` in
`src/editor/CMakeLists.txt`.

- [ ] **Step 4: Run the adapter tests**

Run:

```bash
cmake --build build --target test_lxe_editor_render_view
ctest --test-dir build --output-on-failure -R '^test_lxe_editor_render_view$'
```

Expected: PASS.

- [ ] **Step 5: Commit the adapter**

Run:

```bash
git add src/editor/render src/editor/CMakeLists.txt src/test/CMakeLists.txt src/test/integration/test_lxe_editor_render_view.cpp
git commit -m "test: add lxe editor render view adapter"
```

---

### Task 4: Add Renderer-Neutral Live View API And Backend Submission Stats

**Files:**
- Create: `src/core/rhi/live_render_view.hpp`
- Modify: `src/core/rhi/renderer.hpp`
- Modify: `src/core/gpu/engine_loop.hpp`
- Modify: `src/core/gpu/engine_loop.cpp`
- Modify: `src/backend/vulkan/vulkan_renderer_types.hpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.hpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/vulkan_renderer.hpp`
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/test/integration/test_engine_loop.cpp`

- [ ] **Step 1: Add core live-view data types**

Create `src/core/rhi/live_render_view.hpp`:

```cpp
#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <optional>
#include <string>

namespace LX_core::gpu {

struct LiveRenderView final {
  std::string cameraPath;
  CameraResource cameraResource;
  VisibilityLayerMask visibleMask = Layer_All & ~Layer_EditorOverlay;
  Vec2f viewportExtent{0.0f, 0.0f};
  bool previewEnabled = false;
  bool editorOverlayVisible = true;
};

struct LiveRenderSubmissionStats final {
  usize compilerInputCount = 0;
  usize acceptedInputCount = 0;
  usize rejectedInputCount = 0;
  usize submittedDrawCount = 0;
  usize submittedDispatchCount = 0;
  usize fallbackObservedCount = 0;
  usize descPipelineLookupCount = 0;
  usize descBoundInputCount = 0;
  usize descExecutedInputCount = 0;
  usize bindlessSceneDescriptorCount = 0;
  bool usedExplicitCamera = false;
  bool usedBindlessSceneDescriptors = false;
};

} // namespace LX_core::gpu
```

- [ ] **Step 2: Extend `Renderer` with default no-op live view methods**

In `src/core/rhi/renderer.hpp`, include the new header and add methods to
`class Renderer` after `draw()`:

```cpp
#include "core/rhi/live_render_view.hpp"
```

```cpp
  virtual void setLiveRenderView(std::optional<LiveRenderView> view) {
    (void)view;
  }

  [[nodiscard]] virtual LiveRenderSubmissionStats
  liveRenderSubmissionStats() const {
    return {};
  }
```

The methods are intentionally not pure virtual so existing fake renderers remain
small and non-live renderers keep a safe default.

- [ ] **Step 3: Let `EngineLoop` carry the latest live view**

In `src/core/gpu/engine_loop.hpp`, add:

```cpp
#include <optional>
```

Add public methods:

```cpp
  void setLiveRenderView(std::optional<LiveRenderView> view);
  [[nodiscard]] LiveRenderSubmissionStats liveRenderSubmissionStats() const;
```

Add a private member:

```cpp
  std::optional<LiveRenderView> m_liveRenderView;
```

In `src/core/gpu/engine_loop.cpp`, reset the member in `initialize(...)`:

```cpp
  m_liveRenderView.reset();
```

Add implementations:

```cpp
void EngineLoop::setLiveRenderView(std::optional<LiveRenderView> view) {
  m_liveRenderView = std::move(view);
  if (m_renderer) {
    m_renderer->setLiveRenderView(m_liveRenderView);
  }
}

LiveRenderSubmissionStats EngineLoop::liveRenderSubmissionStats() const {
  return m_renderer ? m_renderer->liveRenderSubmissionStats()
                    : LiveRenderSubmissionStats{};
}
```

In `EngineLoop::startScene(...)`, after `m_renderer->initScene(m_scene);`, add:

```cpp
  m_renderer->setLiveRenderView(m_liveRenderView);
```

In `EngineLoop::tickFrame()`, just before `m_renderer->uploadData();`, add:

```cpp
  m_renderer->setLiveRenderView(m_liveRenderView);
```

- [ ] **Step 4: Update Vulkan renderer wrappers**

In `src/backend/vulkan/vulkan_renderer.hpp` and
`src/backend/vulkan/vulkan_realtime_renderer.hpp`, add overrides:

```cpp
  void setLiveRenderView(std::optional<gpu::LiveRenderView> view) override;
  [[nodiscard]] gpu::LiveRenderSubmissionStats
  liveRenderSubmissionStats() const override;
```

In `src/backend/vulkan/vulkan_renderer.cpp`, forward them:

```cpp
void VulkanRenderer::setLiveRenderView(
    std::optional<gpu::LiveRenderView> view) {
  p_realtime->setLiveRenderView(std::move(view));
}

gpu::LiveRenderSubmissionStats
VulkanRenderer::liveRenderSubmissionStats() const {
  return p_realtime->liveRenderSubmissionStats();
}
```

- [ ] **Step 5: Make live Vulkan pass preparation consume explicit view facts**

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, add private members to
`VulkanRealtimeRenderer::Impl`:

```cpp
  std::optional<LX_core::gpu::LiveRenderView> m_liveRenderView;
  LX_core::gpu::LiveRenderSubmissionStats m_currentLiveStats;
  LX_core::gpu::LiveRenderSubmissionStats m_lastLiveStats;
```

Add helper functions inside `Impl`:

```cpp
  [[nodiscard]] static bool isBindlessSceneDescriptor(
      const LX_core::DescriptorResourceRef &descriptor) {
    const LX_core::StringID name = descriptor.getBindingName();
    return name == LX_core::StringID("SceneObjects") ||
           name == LX_core::StringID("SceneDraws") ||
           name == LX_core::StringID("SceneMaterials") ||
           name == LX_core::StringID("SceneMaterialRefs") ||
           name == LX_core::StringID("SceneSourceMaterialRecords") ||
           name == LX_core::StringID("SceneTextures");
  }

  void recordLiveDescStats(const LX_core::RenderInputDesc &desc) {
    ++m_currentLiveStats.compilerInputCount;
    if (desc.accepted()) {
      ++m_currentLiveStats.acceptedInputCount;
      m_currentLiveStats.submittedDrawCount += desc.stats.submittedDrawCount;
      m_currentLiveStats.submittedDispatchCount +=
          desc.stats.submittedDispatchCount;
      m_currentLiveStats.fallbackObservedCount +=
          desc.stats.fallbackObservedCount;
      for (const LX_core::DescriptorResourceRef &descriptor :
           desc.bindingPlan.descriptors) {
        if (isBindlessSceneDescriptor(descriptor)) {
          ++m_currentLiveStats.bindlessSceneDescriptorCount;
        }
      }
    } else {
      ++m_currentLiveStats.rejectedInputCount;
    }
  }
```

In `makeRealtimeRenderWorkOptionsForCompiledPass(...)`, before returning:

```cpp
    if (m_liveRenderView.has_value()) {
      options.cameraResource = m_liveRenderView->cameraResource;
      options.visibleMask = m_liveRenderView->visibleMask;
    }
```

At the start of `draw()`, after `const VkExtent2D extent = ...`, reset stats:

```cpp
    m_currentLiveStats = {};
    m_currentLiveStats.usedExplicitCamera = m_liveRenderView.has_value();
```

In `drawPassQueue(...)`, for every desc, call `recordLiveDescStats(desc);`
before the accepted check. Increment live execution stats in the accepted branch:

```cpp
      ++m_currentLiveStats.descPipelineLookupCount;
      auto pipeline = resourceManager().getOrCreatePipeline(desc);
      cmd.bindPipeline(pipeline);
      cmd.bindResources(resourceManager(), pipeline, input, desc);
      ++m_currentLiveStats.descBoundInputCount;
      cmd.executeRenderInput(input, desc);
      ++m_currentLiveStats.descExecutedInputCount;
```

At the end of `draw()`, after `m_frameIndex++;`, add:

```cpp
    m_currentLiveStats.usedBindlessSceneDescriptors =
        m_currentLiveStats.bindlessSceneDescriptorCount > 0;
    m_lastLiveStats = m_currentLiveStats;
```

Add `Impl` methods:

```cpp
  void setLiveRenderView(std::optional<LX_core::gpu::LiveRenderView> view) {
    m_liveRenderView = std::move(view);
  }

  [[nodiscard]] LX_core::gpu::LiveRenderSubmissionStats
  liveRenderSubmissionStats() const {
    return m_lastLiveStats;
  }
```

Add public `VulkanRealtimeRenderer` methods that forward to `p_impl`.

- [ ] **Step 6: Run build and engine loop tests**

Run:

```bash
cmake --build build --target test_engine_loop lxe_editor
ctest --test-dir build --output-on-failure -R '^test_engine_loop$'
```

Expected: PASS.

- [ ] **Step 7: Commit renderer live-view API**

Run:

```bash
git add src/core/rhi/live_render_view.hpp src/core/rhi/renderer.hpp src/core/gpu/engine_loop.* src/backend/vulkan/vulkan_renderer.* src/backend/vulkan/vulkan_realtime_renderer.* src/backend/vulkan/vulkan_renderer_types.hpp src/test/integration/test_engine_loop.cpp
git commit -m "feat: add explicit live render view"
```

---

### Task 5: Wire Editor Render View Into The Live Frame Loop

**Files:**
- Modify: `src/editor/main.cpp`
- Modify: `src/editor/app/editor_session.hpp`
- Modify: `src/editor/app/editor_session.cpp`

- [ ] **Step 1: Add session helper to build the current live view**

In `src/editor/app/editor_session.hpp`, include:

```cpp
#include "core/rhi/live_render_view.hpp"
```

Add a public method:

```cpp
  [[nodiscard]] std::optional<LX_core::gpu::LiveRenderView>
  buildLiveRenderView() const;
```

In `src/editor/app/editor_session.cpp`, include:

```cpp
#include "editor/render/editor_render_view.hpp"
```

Add:

```cpp
std::optional<LX_core::gpu::LiveRenderView>
LxeEditorSession::buildLiveRenderView() const {
  const auto editorView =
      LX_editor::buildEditorRenderView(m_editorState, *m_runtime.scene(),
                                       m_windowSize);
  if (!editorView.has_value()) {
    return std::nullopt;
  }

  return LX_core::gpu::LiveRenderView{
      .cameraPath = editorView->cameraPath,
      .cameraResource = editorView->cameraResource,
      .visibleMask = editorView->visibleMask,
      .viewportExtent = editorView->viewportExtent,
      .previewEnabled = editorView->previewEnabled,
      .editorOverlayVisible = editorView->editorOverlayVisible,
  };
}
```

- [ ] **Step 2: Set the live render view every update frame**

In `src/editor/main.cpp`, inside the update hook after camera matrices and
procedural material updates, add:

```cpp
      loop.setLiveRenderView(session.buildLiveRenderView());
```

Place it after:

```cpp
      session.gameCamera().updateMatrices();
      (void)session.runtime().updateProceduralMaterials(...);
```

and before selection/camera input processing. This keeps the renderer view facts
fresh before `EngineLoop::tickFrame()` calls `uploadData()` and `draw()`.

- [ ] **Step 3: Keep scene-open flush synchronized**

In `LxeEditorSession::flushPendingSceneOpen(...)`, after successful
`loop.startScene(nextRuntime.scene());` and after the fallback restore
`loop.startScene(m_runtime.scene());`, call:

```cpp
    loop.setLiveRenderView(buildLiveRenderView());
```

This ensures the first frame after a scene load does not reuse the previous
scene's live view.

- [ ] **Step 4: Run editor render-view and interaction tests**

Run:

```bash
cmake --build build --target lxe_editor test_lxe_editor_render_view test_lxe_editor_interaction
ctest --test-dir build --output-on-failure -R 'test_lxe_editor_render_view|test_lxe_editor_interaction'
```

Expected: PASS.

- [ ] **Step 5: Commit editor live-view wiring**

Run:

```bash
git add src/editor/main.cpp src/editor/app/editor_session.* src/editor/render
git commit -m "fix: wire editor live render view"
```

---

### Task 6: Expose Live Render Stats Through Command/API Surface

**Files:**
- Modify: `src/editor/app/editor_session.hpp`
- Modify: `src/editor/app/editor_session.cpp`
- Modify: `src/editor/api/lxe_editor_api_protocol.hpp`
- Modify: `src/editor/api/lxe_editor_api_protocol.cpp`
- Modify: `src/editor/api/lxe_editor_api_service.hpp`
- Modify: `src/editor/api/lxe_editor_api_service.cpp`
- Modify: `src/editor/commands/render_debug_commands.cpp`
- Modify: `src/test/integration/test_lxe_editor_api_service.cpp`
- Modify: `tests/lxe_editor/api_client.py`

- [ ] **Step 1: Add a command hook for live stats**

In `LxeEditorSession::RenderDebugCommandHooks`, add:

```cpp
    std::function<LX_core::gpu::LiveRenderSubmissionStats()>
        liveRenderSubmissionStats;
```

When constructing session hooks in `src/editor/main.cpp`, bind:

```cpp
          .liveRenderSubmissionStats =
              [&loop]() { return loop.liveRenderSubmissionStats(); },
```

- [ ] **Step 2: Add `render debug live-stats` command**

In `LxeEditorSession::initialize(...)`, extend the render command usage string
to include `render debug live-stats`.

Before the existing `render debug stats <target>` branch, add:

```cpp
        if (args.size() == 2 && args[0] == "debug" &&
            args[1] == "live-stats") {
          if (!m_renderDebugCommandHooks.liveRenderSubmissionStats) {
            return makeCommandError("render debug live-stats unavailable");
          }
          const auto stats =
              m_renderDebugCommandHooks.liveRenderSubmissionStats();
          std::ostringstream structured;
          structured << "{\"compilerInputCount\":"
                     << stats.compilerInputCount
                     << ",\"acceptedInputCount\":"
                     << stats.acceptedInputCount
                     << ",\"rejectedInputCount\":"
                     << stats.rejectedInputCount
                     << ",\"submittedDrawCount\":"
                     << stats.submittedDrawCount
                     << ",\"submittedDispatchCount\":"
                     << stats.submittedDispatchCount
                     << ",\"fallbackObservedCount\":"
                     << stats.fallbackObservedCount
                     << ",\"descPipelineLookupCount\":"
                     << stats.descPipelineLookupCount
                     << ",\"descBoundInputCount\":"
                     << stats.descBoundInputCount
                     << ",\"descExecutedInputCount\":"
                     << stats.descExecutedInputCount
                     << ",\"bindlessSceneDescriptorCount\":"
                     << stats.bindlessSceneDescriptorCount
                     << ",\"usedExplicitCamera\":"
                     << (stats.usedExplicitCamera ? "true" : "false")
                     << ",\"usedBindlessSceneDescriptors\":"
                     << (stats.usedBindlessSceneDescriptors ? "true" : "false")
                     << "}";
          return makeCommandOk("render debug live-stats", structured.str());
        }
```

- [ ] **Step 3: Add API client convenience method**

In `tests/lxe_editor/api_client.py`, add:

```python
    def live_render_stats(self) -> dict[str, Any]:
        return self.decode_structured_json(self.command("render debug live-stats"))
```

- [ ] **Step 4: Add service/command tests**

In `src/test/integration/test_lxe_editor_api_service.cpp`, add a test that
dispatches `render debug live-stats` through the command bus with a hook that
returns:

```cpp
LX_core::gpu::LiveRenderSubmissionStats{
    .compilerInputCount = 1,
    .acceptedInputCount = 1,
    .submittedDrawCount = 1,
    .descPipelineLookupCount = 1,
    .descBoundInputCount = 1,
    .descExecutedInputCount = 1,
    .bindlessSceneDescriptorCount = 6,
    .usedExplicitCamera = true,
    .usedBindlessSceneDescriptors = true,
}
```

Assert that structured JSON contains:

```text
"usedExplicitCamera":true
"usedBindlessSceneDescriptors":true
"bindlessSceneDescriptorCount":6
```

- [ ] **Step 5: Run API tests**

Run:

```bash
cmake --build build --target test_lxe_editor_api_service
ctest --test-dir build --output-on-failure -R '^test_lxe_editor_api_service$'
```

Expected: PASS.

- [ ] **Step 6: Commit live stats command**

Run:

```bash
git add src/editor/app/editor_session.* src/editor/main.cpp src/editor/api src/test/integration/test_lxe_editor_api_service.cpp tests/lxe_editor/api_client.py
git commit -m "feat: expose editor live render stats"
```

---

### Task 7: Add Helmet Startup Live Viewport Smoke

**Files:**
- Create: `tests/lxe_editor/test_live_viewport.py`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add Python black-box test**

Create `tests/lxe_editor/test_live_viewport.py`:

```python
from __future__ import annotations

import unittest

from tests.lxe_editor.api_client import LxeEditorHarness


class LiveViewportBlackBoxTest(unittest.TestCase):
    def test_helmet_scene_loads_nonblack_live_bindless_viewport(self) -> None:
        harness = LxeEditorHarness()
        try:
            harness.start()
        except FileNotFoundError as exc:
            raise unittest.SkipTest(str(exc)) from exc
        except Exception as exc:
            raise unittest.SkipTest(f"unable to launch API target: {exc}") from exc

        try:
            response = harness.client.command(
                "scene import assets/scenes/generated/helmet_standard_pbr.scene.yaml "
                "helmet_standard_pbr --set-active"
            )
            self.assertTrue(response.get("ok"), response)

            scene = harness.client.wait_for(
                lambda: (
                    result
                    if (
                        result := harness.client.get_scene()
                    ).get("sceneName")
                    == "Helmet Standard PBR"
                    else None
                ),
                timeout_s=20.0,
            )
            self.assertEqual(scene["sceneName"], "Helmet Standard PBR")

            def live_frame_ready() -> dict[str, object] | None:
                color = harness.client.decode_structured_json(
                    harness.client.command("render debug stats hdr.color")
                )
                depth = harness.client.decode_structured_json(
                    harness.client.command("render debug stats depth.main")
                )
                stats = harness.client.live_render_stats()
                color_ratio = float(color.get("stats", {}).get("nonZeroRatio", 0.0))
                depth_min = float(depth.get("stats", {}).get("min", 1.0))
                if (
                    color_ratio > 0.0
                    and depth_min < 1.0
                    and stats.get("usedExplicitCamera") is True
                    and stats.get("usedBindlessSceneDescriptors") is True
                    and int(stats.get("bindlessSceneDescriptorCount", 0)) > 0
                    and int(stats.get("fallbackObservedCount", 1)) == 0
                ):
                    return {
                        "color": color,
                        "depth": depth,
                        "stats": stats,
                    }
                return None

            result = harness.client.wait_for(live_frame_ready, timeout_s=30.0)
            self.assertGreater(
                float(result["color"]["stats"]["nonZeroRatio"]),
                0.0,
            )
            self.assertLess(float(result["depth"]["stats"]["min"]), 1.0)
            self.assertTrue(result["stats"]["usedExplicitCamera"])
            self.assertTrue(result["stats"]["usedBindlessSceneDescriptors"])
            self.assertEqual(int(result["stats"]["fallbackObservedCount"]), 0)
        finally:
            harness.close()
```

- [ ] **Step 2: Ensure Python black-box ctest runs this file**

`lx_register_python_ctest(test_lxe_editor_api_blackbox)` already discovers
`tests/lxe_editor/test_*.py`, so no new CTest target is required. Confirm
`tests/lxe_editor/test_live_viewport.py` matches that pattern.

- [ ] **Step 3: Run under Xvfb**

Run:

```bash
cmake --build build --target lxe_editor
xvfb-run -a ctest --test-dir build --output-on-failure -R '^test_lxe_editor_api_blackbox$'
```

Expected: PASS, including `LiveViewportBlackBoxTest`.

- [ ] **Step 4: Commit live viewport smoke**

Run:

```bash
git add tests/lxe_editor/test_live_viewport.py src/test/CMakeLists.txt
git commit -m "test: cover helmet live viewport startup"
```

---

### Task 8: Run Full Verification And Remote MCP Check

**Files:**
- No code changes expected unless verification reveals a defect.

- [ ] **Step 1: Build required targets**

Run:

```bash
cmake --build build --target lxe_editor BuildTest
```

Expected: build succeeds without new warnings in touched targets.

- [ ] **Step 2: Run required CTest coverage**

Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_lxe_editor_source_boundary|test_lxe_editor_render_view|test_command_bus|test_command_bus_v2|test_editor_multi_select|test_inspector_panel|test_scene_tree_panel|test_viewport_overlay|test_lxe_editor_layout|test_lxe_editor_api_service|test_lxe_editor_api_server|test_lxe_editor_interaction|test_realtime_render_profile_commands|test_bindless_indirect_contract|test_bindless_validation_contract|test_helmet_standard_pbr_realtime_smoke'
```

Expected: PASS.

- [ ] **Step 3: Run video-device coverage under Xvfb**

Run:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -R '^test_lxe_editor_api_blackbox$|^test_helmet_standard_pbr_realtime_smoke$'
```

Expected: PASS.

- [ ] **Step 4: Run hard-cut and legacy fallback audits**

Run:

```bash
rg -n "core/editor" src
rg -n "#include \"core/editor|#include <core/editor" src
rg -n "demos/lxe_editor|src/demos/lxe_editor" CMakeLists.txt src
rg -n "CommandBus|EditorState|ViewportOverlay|InspectorPanel|SceneTreePanel" src/core
rg -n "MaterialUBO|LegacyPerItem|fallback material|fallback.*material|non-bindless" src/editor src/backend src/core
rg -n "core/editor|demos/lxe_editor|lxe_editor/editor|forwarding header|old editor|legacy editor|compat.*editor" CMakeLists.txt src
```

Expected:

- first, second, third, fourth, and sixth commands have no production hits;
- the legacy/fallback audit has no positive live editor path hits; named
  negative tests such as `test_071g_legacy_boundary_removal.cpp` may still
  mention legacy tokens outside `src/editor`.

- [ ] **Step 5: Remote MCP verification**

Use the lxe_manager MCP tools:

1. `ops.build_target` for `lxe_editor`.
2. `ops.editor_restart`.
3. `editor.command` with:
   `scene import assets/scenes/generated/helmet_standard_pbr.scene.yaml helmet_standard_pbr --set-active`
4. `editor.wait_for` until `state scene` reports `Helmet Standard PBR`.
5. `editor.command` with `render debug stats hdr.color`.
6. `editor.command` with `render debug stats depth.main`.
7. `editor.command` with `render debug live-stats`.

Expected MCP evidence:

- `hdr.color.stats.nonZeroRatio > 0`
- `depth.main.stats.min < 1`
- `live-stats.usedExplicitCamera == true`
- `live-stats.usedBindlessSceneDescriptors == true`
- `live-stats.bindlessSceneDescriptorCount > 0`
- `live-stats.fallbackObservedCount == 0`

- [ ] **Step 6: Commit verification fixes if any**

If verification required code changes, commit them:

```bash
git add <changed-files>
git commit -m "fix: stabilize lxe editor live viewport verification"
```

If no fixes were needed, do not create an empty commit.

## Self-Review Checklist

- Spec coverage:
  - `src/editor` promotion: Task 2.
  - hard-cut old code: Tasks 1, 2, 8.
  - live viewport explicit camera/mask: Tasks 3, 4, 5.
  - bindless proof: Tasks 4, 6, 7, 8.
  - startup smoke: Task 7.
  - remote MCP verification: Task 8.
  - reusable render flow remains in core/backend: Tasks 4 and 5 keep editor to
    adapter-only code.
- Placeholder scan: no `TBD`, `TODO`, or open-ended "fix later" steps.
- Type consistency:
  - editor adapter uses `LX_editor::EditorRenderView`.
  - renderer-neutral API uses `LX_core::gpu::LiveRenderView`.
  - backend and command stats use `LX_core::gpu::LiveRenderSubmissionStats`.
- Risk note:
  - This plan preserves existing C++ namespaces during the hard-cut move to keep
    the migration reviewable. It still removes old source ownership, old include
    paths, old CMake ownership, and live non-bindless fallback routes.
