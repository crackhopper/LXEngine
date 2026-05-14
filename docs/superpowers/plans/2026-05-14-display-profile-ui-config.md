# Per-Display UI Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-display `lxe_editor` UI profiles, command-line display selection, remote display config control, and manager MCP self-restart.

**Architecture:** Add a backend-neutral display catalog at the window boundary, then make `EditorConfigState` load/save `version: 2` configs as `displayDefault` plus per-display overrides. `lxe_editor` binds one effective profile at startup; editor commands/API/manager MCP expose display config mutation, while `ops.manager_restart` handles manager-side tool updates.

**Tech Stack:** C++20, yaml-cpp, SDL3/GLFW display APIs, existing `CommandBus`/HTTP API, TypeScript Node manager, Vitest, CMake/Ninja.

---

## File Structure

- Modify `src/core/platform/window.hpp`
  Add `DisplayInfo`, display key/label helpers, and selected-display startup placement helpers.
- Modify `src/infra/window/window.hpp`
  Add static display enumeration and selected-display constructor option.
- Modify `src/infra/window/sdl_window.cpp`
  Implement display enumeration with SDL3 and create windows on selected display.
- Modify `src/infra/window/glfw_window.cpp`
  Implement monitor enumeration with GLFW and create windows on selected display.
- Modify `src/demos/lxe_editor/editor_config_state.hpp/.cpp`
  Add v2 YAML model, v1 migration, display sync, effective config composition, and override diff saving.
- Modify `src/demos/lxe_editor/main.cpp`
  Parse `--display-list` and `--display`, select startup display, bind active profile, and save profile overrides.
- Modify `src/demos/lxe_editor/lxe_editor_commands.hpp/.cpp`
  Add display commands backed by config hooks.
- Modify `src/demos/lxe_editor/lxe_editor_api_service.hpp/.cpp` and `src/demos/lxe_editor/lxe_editor_api_server.cpp`
  Expose typed display config API methods/endpoints for manager MCP forwarding.
- Modify `tools/lxe_manager/src/editor/editor-client.ts`
  Add display API client methods.
- Modify `tools/lxe_manager/src/mcp/server.ts`
  Add display tools and `ops.manager_restart`.
- Create `tools/lxe_manager/src/ops/manager-ops.ts`
  Encapsulate manager self-restart response and process exit behavior.
- Modify `tools/lxe_manager/src/index.ts`
  Wire manager ops into MCP handlers.
- Modify `scripts/lxe_manager/start_mcp.sh` and `scripts/lxe_manager/start_mcp.ps1`
  Add restart-loop support around the Node manager process.
- Add/modify tests:
  `src/test/integration/test_lxe_editor_display_config.cpp`,
  `src/test/integration/test_lxe_editor_layout.cpp`,
  `tools/lxe_manager/tests/mcp-server.test.ts`,
  `tools/lxe_manager/tests/manager-ops.test.ts`.
- Update docs after implementation:
  `src/demos/lxe_editor/README.md`,
  `notes/tools/lxe-manager-mcp.md`.

## Task 1: Display Catalog Types And Pure Helpers

**Files:**
- Modify: `src/core/platform/window.hpp`
- Test: `src/test/integration/test_lxe_editor_display_config.cpp`

- [ ] **Step 1: Write failing tests for display key, label, and default placement**

Add this new test file:

```cpp
#include "core/platform/window.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg \
                << " (" #cond ")\n";                                          \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testDisplayKeyAndLabelAreStableFallbacks() {
  LX_core::DisplayInfo display;
  display.index = 1;
  display.backend = "sdl";
  display.name = "DELL U2720Q";
  display.usableBounds = {.x = 1920, .y = 0, .width = 3840, .height = 2160};
  display.contentScale = 1.5f;

  LX_core::finalizeDisplayInfo(display);

  EXPECT(display.key == "sdl:1:DELL U2720Q:3840x2160:1.50",
         "display key should include backend, index, name, usable size, scale");
  EXPECT(display.label == "1: DELL U2720Q (3840x2160 @ 1.50x)",
         "display label should be human readable");
}

void testDefaultPlacementUsesSelectedDisplayUsableBounds() {
  const LX_core::DisplayInfo display{
      .index = 2,
      .backend = "sdl",
      .name = "Side",
      .bounds = {.x = 1900, .y = 0, .width = 2560, .height = 1440},
      .usableBounds = {.x = 1920, .y = 40, .width = 2520, .height = 1360},
      .contentScale = 1.0f,
      .key = "sdl:2:Side:2520x1360:1.00",
      .label = "2: Side (2520x1360 @ 1.00x)",
  };

  const auto placement = LX_core::makeDefaultWindowPlacementForDisplay(
      display, 1280, 720);

  EXPECT(placement.width == 1280, "default placement should keep width");
  EXPECT(placement.height == 720, "default placement should keep height");
  EXPECT(placement.x >= display.usableBounds.x,
         "default placement should start within usable x bounds");
  EXPECT(placement.y >= display.usableBounds.y,
         "default placement should start within usable y bounds");
}

} // namespace

int main() {
  testDisplayKeyAndLabelAreStableFallbacks();
  testDefaultPlacementUsesSelectedDisplayUsableBounds();
  if (failures != 0) {
    std::cerr << failures << " display config test(s) failed\n";
    return 1;
  }
  return 0;
}
```

- [ ] **Step 2: Register the new C++ test target**

Modify `src/test/integration/CMakeLists.txt` by adding the new source to the same integration-test pattern used by neighboring `test_lxe_editor_layout.cpp`. If the file uses explicit targets, add:

```cmake
add_executable(test_lxe_editor_display_config test_lxe_editor_display_config.cpp)
target_link_libraries(test_lxe_editor_display_config PRIVATE LXCore LXInfra)
add_test(NAME test_lxe_editor_display_config COMMAND test_lxe_editor_display_config)
set_tests_properties(test_lxe_editor_display_config PROPERTIES LABELS "auto")
```

If the local CMake file uses a helper function, use that helper with the same target name and labels.

- [ ] **Step 3: Run the new test and verify it fails to compile**

Run:

```bash
cmake --build build --target test_lxe_editor_display_config
```

Expected: compile failure mentioning `DisplayInfo`, `finalizeDisplayInfo`, or `makeDefaultWindowPlacementForDisplay` is not declared.

- [ ] **Step 4: Add display catalog helpers**

Add this near the existing placement structs in `src/core/platform/window.hpp`:

```cpp
struct DisplayInfo final {
  int index = 0;
  std::string backend;
  std::string name;
  WindowUsableBounds bounds;
  WindowUsableBounds usableBounds;
  float contentScale = 1.0f;
  std::string key;
  std::string label;
};

[[nodiscard]] inline std::string formatDisplayScale(const float scale) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << scale;
  return oss.str();
}

inline void finalizeDisplayInfo(DisplayInfo& display) {
  const std::string safeName = display.name.empty() ? "Display" : display.name;
  const std::string scale = formatDisplayScale(display.contentScale);
  display.key = display.backend + ":" + std::to_string(display.index) + ":" +
                safeName + ":" + std::to_string(display.usableBounds.width) +
                "x" + std::to_string(display.usableBounds.height) + ":" + scale;
  display.label = std::to_string(display.index) + ": " + safeName + " (" +
                  std::to_string(display.usableBounds.width) + "x" +
                  std::to_string(display.usableBounds.height) + " @ " + scale +
                  "x)";
}

[[nodiscard]] inline WindowPlacement makeDefaultWindowPlacementForDisplay(
    const DisplayInfo& display, const int width, const int height) {
  WindowPlacement placement;
  placement.width = std::min(width, display.usableBounds.width);
  placement.height = std::min(height, display.usableBounds.height);
  placement.x = display.usableBounds.x +
                std::max(0, display.usableBounds.width - placement.width) / 2;
  placement.y = display.usableBounds.y +
                std::max(0, display.usableBounds.height - placement.height) / 2;
  placement.maximized = false;
  return placement;
}
```

Also add includes:

```cpp
#include <iomanip>
#include <sstream>
#include <string>
```

- [ ] **Step 5: Run the test and verify it passes**

Run:

```bash
cmake --build build --target test_lxe_editor_display_config
ctest --test-dir build --output-on-failure -R test_lxe_editor_display_config
```

Expected: build succeeds and CTest reports the test passed.

- [ ] **Step 6: Commit**

```bash
git add src/core/platform/window.hpp src/test/integration/CMakeLists.txt src/test/integration/test_lxe_editor_display_config.cpp
git commit -m "feat: add display catalog helpers"
```

## Task 2: Window Backend Display Enumeration

**Files:**
- Modify: `src/infra/window/window.hpp`
- Modify: `src/infra/window/sdl_window.cpp`
- Modify: `src/infra/window/glfw_window.cpp`
- Test: `src/test/integration/test_lxe_editor_display_config.cpp`

- [ ] **Step 1: Add failing compile coverage for static enumeration API**

Append this test to `test_lxe_editor_display_config.cpp`:

```cpp
#include "infra/window/window.hpp"

void testWindowCanEnumerateDisplays() {
  const auto displays = LX_infra::Window::enumerateDisplays();
  EXPECT(!displays.empty(), "window backend should report at least one display");
  EXPECT(!displays.front().key.empty(), "display key should be populated");
  EXPECT(!displays.front().label.empty(), "display label should be populated");
}
```

Call it from `main()` after the pure helper tests.

- [ ] **Step 2: Run the test and verify it fails to compile**

Run:

```bash
cmake --build build --target test_lxe_editor_display_config
```

Expected: compile failure for missing `LX_infra::Window::enumerateDisplays`.

- [ ] **Step 3: Add the public API**

In `src/infra/window/window.hpp`, add:

```cpp
struct WindowCreateOptions final {
  std::optional<LX_core::WindowPlacement> initialPlacement;
  std::optional<std::string> displayKey;
};
```

Change the constructor declarations to keep compatibility and add selected-display options:

```cpp
Window(const char *title, int width, int height,
       std::optional<LX_core::WindowPlacement> initialPlacement = std::nullopt);
Window(const char *title, int width, int height,
       const WindowCreateOptions& options);

[[nodiscard]] static std::vector<LX_core::DisplayInfo> enumerateDisplays();
```

- [ ] **Step 4: Implement SDL enumeration**

In `src/infra/window/sdl_window.cpp`, add a helper:

```cpp
[[nodiscard]] std::vector<LX_core::DisplayInfo> enumerateSdlDisplays() {
  std::vector<LX_core::DisplayInfo> result;
  int displayCount = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
  if (displays == nullptr || displayCount <= 0) {
    return result;
  }

  result.reserve(static_cast<usize>(displayCount));
  for (int i = 0; i < displayCount; ++i) {
    SDL_Rect bounds{};
    SDL_Rect usable{};
    (void)SDL_GetDisplayBounds(displays[i], &bounds);
    (void)SDL_GetDisplayUsableBounds(displays[i], &usable);
    const char* name = SDL_GetDisplayName(displays[i]);
    LX_core::DisplayInfo info;
    info.index = i;
    info.backend = "sdl";
    info.name = name ? name : "Display";
    info.bounds = {.x = bounds.x, .y = bounds.y, .width = bounds.w, .height = bounds.h};
    info.usableBounds = {.x = usable.x, .y = usable.y, .width = usable.w, .height = usable.h};
    info.contentScale = SDL_GetDisplayContentScale(displays[i]);
    if (info.usableBounds.width <= 0 || info.usableBounds.height <= 0) {
      info.usableBounds = info.bounds;
    }
    if (info.contentScale <= 0.0f) {
      info.contentScale = 1.0f;
    }
    LX_core::finalizeDisplayInfo(info);
    result.push_back(std::move(info));
  }
  SDL_free(displays);
  return result;
}
```

Return it from `Window::enumerateDisplays()`.

- [ ] **Step 5: Implement GLFW enumeration**

In `src/infra/window/glfw_window.cpp`, add equivalent monitor enumeration:

```cpp
[[nodiscard]] std::vector<LX_core::DisplayInfo> enumerateGlfwDisplays() {
  std::vector<LX_core::DisplayInfo> result;
  int monitorCount = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
  if (monitors == nullptr || monitorCount <= 0) {
    return result;
  }

  result.reserve(static_cast<usize>(monitorCount));
  for (int i = 0; i < monitorCount; ++i) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    glfwGetMonitorWorkarea(monitors[i], &x, &y, &width, &height);
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetMonitorContentScale(monitors[i], &xScale, &yScale);
    const char* name = glfwGetMonitorName(monitors[i]);
    LX_core::DisplayInfo info;
    info.index = i;
    info.backend = "glfw";
    info.name = name ? name : "Display";
    info.bounds = {.x = x, .y = y, .width = width, .height = height};
    info.usableBounds = info.bounds;
    info.contentScale = xScale > 0.0f ? xScale : 1.0f;
    LX_core::finalizeDisplayInfo(info);
    result.push_back(std::move(info));
  }
  return result;
}
```

Return it from `Window::enumerateDisplays()`.

- [ ] **Step 6: Route selected display into initial placement**

In both SDL and GLFW constructors, before creating/showing the window, resolve `WindowCreateOptions`:

```cpp
std::optional<LX_core::WindowPlacement> resolveInitialPlacement(
    int width, int height, const WindowCreateOptions& options) {
  if (options.initialPlacement.has_value()) {
    return options.initialPlacement;
  }
  if (!options.displayKey.has_value()) {
    return std::nullopt;
  }
  for (const auto& display : Window::enumerateDisplays()) {
    if (display.key == *options.displayKey) {
      return LX_core::makeDefaultWindowPlacementForDisplay(display, width, height);
    }
  }
  return std::nullopt;
}
```

Keep the old constructor delegating to the new one:

```cpp
Window::Window(const char* title, int width, int height,
               std::optional<LX_core::WindowPlacement> initialPlacement)
    : Window(title, width, height,
             WindowCreateOptions{.initialPlacement = std::move(initialPlacement)}) {}
```

- [ ] **Step 7: Run display tests**

Run:

```bash
cmake --build build --target test_lxe_editor_display_config
ctest --test-dir build --output-on-failure -R test_lxe_editor_display_config
```

Expected: test passes on the local display environment.

- [ ] **Step 8: Commit**

```bash
git add src/infra/window/window.hpp src/infra/window/sdl_window.cpp src/infra/window/glfw_window.cpp src/test/integration/test_lxe_editor_display_config.cpp
git commit -m "feat: enumerate editor displays"
```

## Task 3: Editor Config V2 Default Plus Override Model

**Files:**
- Modify: `src/demos/lxe_editor/editor_config_state.hpp`
- Modify: `src/demos/lxe_editor/editor_config_state.cpp`
- Test: `src/test/integration/test_lxe_editor_layout.cpp`

- [ ] **Step 1: Add failing v2 config tests**

Append tests to `test_lxe_editor_layout.cpp`:

```cpp
LX_core::DisplayInfo makeTestDisplay(int index, std::string name,
                                     int x, int y, int width, int height,
                                     float scale) {
  LX_core::DisplayInfo display;
  display.index = index;
  display.backend = "sdl";
  display.name = std::move(name);
  display.bounds = {.x = x, .y = y, .width = width, .height = height};
  display.usableBounds = display.bounds;
  display.contentScale = scale;
  LX_core::finalizeDisplayInfo(display);
  return display;
}

void testEditorConfigV2CreatesProfilesForAllDisplays() {
  namespace fs = std::filesystem;
  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_display_profiles";
  fs::remove_all(tempRoot);

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::vector<LX_core::DisplayInfo> displays{
      makeTestDisplay(0, "Panel", 0, 0, 1920, 1080, 1.0f),
      makeTestDisplay(1, "External", 1920, 0, 3840, 2160, 1.5f),
  };

  auto document = state.loadOrCreateForDisplays(displays);
  EXPECT(document.displayProfiles.size() == 2,
         "v2 config should create one profile per display");
  EXPECT(document.activeDisplay == displays.front().key,
         "new config should default active display to first display");
  EXPECT(state.saveDisplayDocument(document, displays.front().key, document.displayDefault),
         "v2 config should save");

  const auto loaded = state.loadOrCreateForDisplays(displays);
  EXPECT(loaded.displayProfiles.size() == 2,
         "saved v2 config should reload display profiles");

  fs::remove_all(tempRoot);
}

void testDisplayOverrideComposesWithDefault() {
  namespace fs = std::filesystem;
  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_display_override";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  const auto display = makeTestDisplay(1, "External", 1920, 0, 3840, 2160, 1.5f);
  std::ofstream(state.configPath())
      << "version: 2\n"
      << "activeDisplay: \"" << display.key << "\"\n"
      << "displayDefault:\n"
      << "  key: display-default\n"
      << "  window:\n"
      << "    width: 1280\n"
      << "    height: 720\n"
      << "  preferences:\n"
      << "    uiFontScale: 1.0\n"
      << "  layout:\n"
      << "    windows:\n"
      << "      - id: Inspector\n"
      << "        visible: true\n"
      << "        x: 10\n"
      << "        y: 20\n"
      << "        width: 300\n"
      << "        height: 400\n"
      << "displayProfiles:\n"
      << "  - key: \"" << display.key << "\"\n"
      << "    label: \"" << display.label << "\"\n"
      << "    available: true\n"
      << "    overrides:\n"
      << "      preferences:\n"
      << "        uiFontScale: 1.25\n"
      << "      layout:\n"
      << "        windows:\n"
      << "          - id: Inspector\n"
      << "            width: 420\n";

  auto document = state.loadOrCreateForDisplays({display});
  const auto effective = state.composeEffectiveConfig(document, display.key);
  EXPECT(effective.preferences.uiFontScale > 1.24f &&
             effective.preferences.uiFontScale < 1.26f,
         "display override should replace font scale");
  const auto inspector =
      LX_demo::lxe_editor::findEditorWindowLayout(effective, "Inspector");
  EXPECT(inspector.has_value(), "effective config should include default panel");
  EXPECT(inspector->get().x == 10, "default panel x should remain");
  EXPECT(inspector->get().width == 420, "override panel width should apply");

  fs::remove_all(tempRoot);
}
```

Call both tests from `main()`.

- [ ] **Step 2: Run tests and verify compile failure**

Run:

```bash
cmake --build build --target test_lxe_editor_layout
```

Expected: missing `loadOrCreateForDisplays`, `saveDisplayDocument`, or `composeEffectiveConfig`.

- [ ] **Step 3: Extend config structs**

In `editor_config_state.hpp`, add:

```cpp
struct EditorDisplayProfile final {
  std::string key;
  std::string label;
  bool available = false;
  EditorConfigDocument overrides;
};

struct EditorDisplayConfigDocument final {
  int version = 2;
  std::string activeDisplay;
  EditorConfigDocument displayDefault;
  std::vector<EditorDisplayProfile> displayProfiles;
};
```

Add methods:

```cpp
[[nodiscard]] EditorDisplayConfigDocument loadOrCreateForDisplays(
    const std::vector<LX_core::DisplayInfo>& displays) const;
bool saveDisplayDocument(const EditorDisplayConfigDocument& document,
                         std::string_view activeDisplayKey,
                         const EditorConfigDocument& effectiveConfig) const;
[[nodiscard]] EditorConfigDocument composeEffectiveConfig(
    const EditorDisplayConfigDocument& document,
    std::string_view displayKey) const;
```

- [ ] **Step 4: Implement v2 load and v1 migration**

In `editor_config_state.cpp`, keep the current v1 loader helpers and add `loadDisplayDocumentV2`. Required behavior:

```cpp
EditorDisplayConfigDocument EditorConfigState::loadOrCreateForDisplays(
    const std::vector<LX_core::DisplayInfo>& displays) const {
  EditorDisplayConfigDocument document;
  document.displayDefault = EditorConfigDocument{};
  if (!std::filesystem::exists(m_configPath)) {
    syncDisplayProfiles(document, displays);
    document.activeDisplay = displays.empty() ? std::string{} : displays.front().key;
    (void)saveDisplayDocument(document, document.activeDisplay, document.displayDefault);
    return document;
  }

  const YAML::Node root = YAML::LoadFile(m_configPath.string());
  const int version = root["version"] ? root["version"].as<int>() : 1;
  if (version == 1) {
    document.displayDefault = loadLegacyConfigDocument(root);
  } else if (version == 2) {
    document = loadDisplayDocumentV2(root);
  } else {
    document = EditorDisplayConfigDocument{};
  }
  syncDisplayProfiles(document, displays);
  if (!isAvailableDisplay(document, document.activeDisplay)) {
    document.activeDisplay = firstAvailableDisplayKey(document);
  }
  return document;
}
```

Use existing `loadWindowPlacement`, `loadLayoutWindow`, `sortUniqueWindows`, and `clampUiFontScale`. Preserve v1 `load()` by making it call `loadLegacyConfigDocument` for compatibility with older tests.

- [ ] **Step 5: Implement compose and diff save**

Implement merge by field:

```cpp
EditorConfigDocument mergeConfig(const EditorConfigDocument& defaults,
                                 const EditorConfigDocument& overrides) {
  EditorConfigDocument result = defaults;
  if (overrides.windowPlacement.has_value()) {
    LX_core::WindowPlacement merged =
        result.windowPlacement.value_or(LX_core::WindowPlacement{});
    const auto& patch = *overrides.windowPlacement;
    if (patch.x != 0) merged.x = patch.x;
    if (patch.y != 0) merged.y = patch.y;
    if (patch.width > 0) merged.width = patch.width;
    if (patch.height > 0) merged.height = patch.height;
    merged.maximized = patch.maximized;
    result.windowPlacement = merged;
  }
  result.preferences.uiFontScale =
      overrides.preferences.uiFontScale != 1.0f
          ? clampUiFontScale(overrides.preferences.uiFontScale)
          : clampUiFontScale(result.preferences.uiFontScale);
  mergeLayoutWindowsById(result.layoutWindows, overrides.layoutWindows);
  return result;
}
```

When saving, compute `overrides = diffConfig(document.displayDefault, effectiveConfig)` and write that only into the matching active display profile. Keep unavailable profiles in the YAML.

- [ ] **Step 6: Run existing and new config tests**

Run:

```bash
cmake --build build --target test_lxe_editor_layout
ctest --test-dir build --output-on-failure -R test_lxe_editor_layout
```

Expected: all layout/config tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/demos/lxe_editor/editor_config_state.hpp src/demos/lxe_editor/editor_config_state.cpp src/test/integration/test_lxe_editor_layout.cpp
git commit -m "feat: persist editor display profiles"
```

## Task 4: lxe_editor Startup Display CLI

**Files:**
- Modify: `src/demos/lxe_editor/main.cpp`
- Test: `src/test/integration/test_lxe_editor_display_config.cpp`

- [ ] **Step 1: Add pure CLI parsing tests**

Add to `test_lxe_editor_display_config.cpp` after Task 1 tests:

```cpp
void testDisplaySelectionChoosesArgumentThenActiveThenFirst() {
  const auto first = makeTestDisplay(0, "Panel", 0, 0, 1920, 1080, 1.0f);
  const auto second = makeTestDisplay(1, "External", 1920, 0, 3840, 2160, 1.5f);
  std::vector<LX_core::DisplayInfo> displays{first, second};

  EXPECT(LX_demo::lxe_editor::selectStartupDisplay(displays, second.key, "").key ==
             second.key,
         "explicit display key should win");
  EXPECT(LX_demo::lxe_editor::selectStartupDisplay(displays, "0", second.key).key ==
             first.key,
         "explicit display index should win");
  EXPECT(LX_demo::lxe_editor::selectStartupDisplay(displays, "", second.key).key ==
             second.key,
         "activeDisplay should be used when no argument is provided");
  EXPECT(LX_demo::lxe_editor::selectStartupDisplay(displays, "", "missing").key ==
             first.key,
         "missing activeDisplay should fall back to first display");
}
```

Call it from `main()`.

- [ ] **Step 2: Run test and verify compile failure**

Run:

```bash
cmake --build build --target test_lxe_editor_display_config
```

Expected: missing `selectStartupDisplay`.

- [ ] **Step 3: Extract startup display helpers**

In `main.cpp`, add internal helpers in the anonymous namespace:

```cpp
struct DisplayLaunchOptions final {
  bool listDisplays = false;
  std::string displaySelector;
};

[[nodiscard]] std::optional<DisplayLaunchOptions>
parseDisplayLaunchOptions(int argc, char** argv, std::string& error);

[[nodiscard]] const LX_core::DisplayInfo& selectStartupDisplay(
    const std::vector<LX_core::DisplayInfo>& displays,
    std::string_view selector,
    std::string_view activeDisplay);
```

Recognize:

```text
--display-list
--display <index-or-key>
```

Reject missing selector with `--display requires an index or key`.

- [ ] **Step 4: Wire startup order**

Change startup in `main.cpp` to:

```cpp
LX_infra::Window::Initialize();
const auto displays = LX_infra::Window::enumerateDisplays();
if (displays.empty()) {
  std::cerr << "[lxe_editor] no displays available\n";
  return 1;
}

demo::EditorConfigState configState(resolveRuntimePath("data/lxe_editor"));
demo::EditorDisplayConfigDocument displayConfig =
    configState.loadOrCreateForDisplays(displays);

if (displayOptions.listDisplays) {
  printDisplayList(displays, displayConfig.activeDisplay);
  return 0;
}

const auto& startupDisplay = demo::selectStartupDisplay(
    displays, displayOptions.displaySelector, displayConfig.activeDisplay);
demo::EditorConfigDocument editorConfig =
    configState.composeEffectiveConfig(displayConfig, startupDisplay.key);
if (!editorConfig.windowPlacement.has_value()) {
  editorConfig.windowPlacement = LX_core::makeDefaultWindowPlacementForDisplay(
      startupDisplay, kWindowWidth, kWindowHeight);
}
auto window = std::make_shared<LX_infra::Window>(
    "lxe_editor", kWindowWidth, kWindowHeight,
    LX_infra::WindowCreateOptions{.initialPlacement = editorConfig.windowPlacement,
                                  .displayKey = startupDisplay.key});
```

On dirty save and final save:

```cpp
(void)configState.saveDisplayDocument(displayConfig, startupDisplay.key,
                                      session.editorConfig());
```

- [ ] **Step 5: Run build**

Run:

```bash
cmake --build build --target lxe_editor test_lxe_editor_display_config
ctest --test-dir build --output-on-failure -R test_lxe_editor_display_config
```

Expected: editor and display tests build and pass.

- [ ] **Step 6: Manual smoke for display list**

Run:

```bash
./build/src/demos/lxe_editor/lxe_editor --display-list
```

Expected: command exits with code 0 and prints at least display index `0`, key, label, usable bounds, and scale. It should not initialize Vulkan.

- [ ] **Step 7: Commit**

```bash
git add src/demos/lxe_editor/main.cpp src/test/integration/test_lxe_editor_display_config.cpp
git commit -m "feat: select editor display at startup"
```

## Task 5: Display Commands And Editor API

**Files:**
- Modify: `src/demos/lxe_editor/lxe_editor_commands.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_commands.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Test: `src/test/integration/test_lxe_editor_api_service.cpp`

- [ ] **Step 1: Add failing command/API service tests**

In `test_lxe_editor_api_service.cpp`, add a service harness hook for display config and tests:

```cpp
void testDisplayCommandsExposeActiveAndList() {
  ApiHarness harness;
  harness.displayListJson = R"({"displays":[{"key":"sdl:0:Panel:1920x1080:1.00","active":true}]})";
  harness.displayActiveJson = R"({"activeDisplay":"sdl:0:Panel:1920x1080:1.00"})";

  auto list = harness.service.executeCommand({"display list"});
  EXPECT(list.ok, "display list command should succeed");
  EXPECT(list.structured.find("\"displays\"") != std::string::npos,
         "display list should return structured display JSON");

  auto active = harness.service.displayActive();
  EXPECT(active.find("\"activeDisplay\"") != std::string::npos,
         "display active API should return active display JSON");
}
```

- [ ] **Step 2: Run test and verify compile failure**

Run:

```bash
cmake --build build --target test_lxe_editor_api_service
```

Expected: missing display hooks/API methods.

- [ ] **Step 3: Add display hooks**

In `lxe_editor_commands.hpp`, add hook aliases to `LxeEditorCommandContext`:

```cpp
using DisplayListJsonFn = std::function<std::string()>;
using DisplayActiveJsonFn = std::function<std::string()>;
using DisplayConfigGetJsonFn = std::function<std::string(std::string_view)>;
using DisplayConfigSetFn =
    std::function<std::string(std::string_view, std::string_view)>;
using DisplaySelectFn = std::function<std::string(std::string_view)>;

DisplayListJsonFn displayListJson;
DisplayActiveJsonFn displayActiveJson;
DisplayConfigGetJsonFn displayConfigGetJson;
DisplayConfigSetFn displayConfigSet;
DisplaySelectFn displaySelect;
```

- [ ] **Step 4: Register display command**

In `lxe_editor_commands.cpp`, add a `display` command:

```cpp
bus.registerCommand(
    "display",
    "display list|active|config get <key|active|default>|config set <key|default> <json>|select <key>",
    [context](const std::vector<std::string>& args) {
      if (args.size() == 1 && args[0] == "list") {
        return makeOk("display list", context.displayListJson());
      }
      if (args.size() == 1 && args[0] == "active") {
        return makeOk("display active", context.displayActiveJson());
      }
      if (args.size() == 3 && args[0] == "config" && args[1] == "get") {
        return makeOk("display config", context.displayConfigGetJson(args[2]));
      }
      if (args.size() >= 4 && args[0] == "config" && args[1] == "set") {
        return makeOk("display config updated",
                      context.displayConfigSet(args[2], joinArgs(args, 3)));
      }
      if (args.size() == 2 && args[0] == "select") {
        return makeOk("display selected", context.displaySelect(args[1]));
      }
      return makeError("usage: display list|active|config get <key|active|default>|config set <key|default> <json>|select <key>");
    });
```

Add a small `joinArgs` helper if one does not already exist.

- [ ] **Step 5: Add typed API service methods**

In `LxeEditorApiService::Hooks`, add matching display hook functions. Add methods:

```cpp
[[nodiscard]] std::string displayList() const;
[[nodiscard]] std::string displayActive() const;
[[nodiscard]] std::string displayConfigGet(const std::string& key) const;
[[nodiscard]] std::string displayConfigSet(const std::string& key,
                                           const std::string& patch);
[[nodiscard]] std::string displaySelect(const std::string& key);
```

Implement each as a thin hook call with `{"ok":false,"error":"display config unavailable"}` fallback when the hook is not set.

- [ ] **Step 6: Add HTTP routes**

In `lxe_editor_api_server.cpp`, add routes:

```text
GET  /api/display/list
GET  /api/display/active
GET  /api/display/config?key=<key|active|default>
POST /api/display/config     {"key":"...","patch":"..."}
POST /api/display/select     {"key":"..."}
```

Return `400` for missing key in config/select routes.

- [ ] **Step 7: Wire hooks in `main.cpp`**

Create lambdas over `displayConfig`, `displays`, `startupDisplay.key`, and `configState`:

```cpp
.displayListJson = [&]() { return demo::displayListJson(displayConfig); },
.displayActiveJson = [&]() { return demo::displayActiveJson(startupDisplay.key); },
.displayConfigGetJson = [&](std::string_view key) {
  return demo::displayConfigGetJson(configState, displayConfig, key, startupDisplay.key);
},
.displayConfigSet = [&](std::string_view key, std::string_view patch) {
  return demo::displayConfigSet(configState, displayConfig, key, patch);
},
.displaySelect = [&](std::string_view key) {
  return demo::displaySelect(configState, displayConfig, key);
},
```

Keep these helper functions in `editor_config_state.cpp` or a new small `display_config_commands.cpp` if `main.cpp` starts to grow too much.

- [ ] **Step 8: Run API and layout tests**

Run:

```bash
cmake --build build --target test_lxe_editor_api_service test_lxe_editor_layout
ctest --test-dir build --output-on-failure -R "test_lxe_editor_api_service|test_lxe_editor_layout"
```

Expected: both tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/demos/lxe_editor/lxe_editor_commands.hpp src/demos/lxe_editor/lxe_editor_commands.cpp src/demos/lxe_editor/lxe_editor_api_service.hpp src/demos/lxe_editor/lxe_editor_api_service.cpp src/demos/lxe_editor/lxe_editor_api_server.cpp src/demos/lxe_editor/main.cpp src/test/integration/test_lxe_editor_api_service.cpp
git commit -m "feat: expose editor display config commands"
```

## Task 6: Manager MCP Display Tools

**Files:**
- Modify: `tools/lxe_manager/src/editor/editor-client.ts`
- Modify: `tools/lxe_manager/src/mcp/server.ts`
- Test: `tools/lxe_manager/tests/mcp-server.test.ts`

- [ ] **Step 1: Add failing manager MCP tests for display tools**

In `tools/lxe_manager/tests/mcp-server.test.ts`, extend `EditorClientSurface` test input with display methods and add:

```ts
it("routes display tools to the editor client", async () => {
  const displayList = vi.fn(async () => ({ displays: [{ key: "sdl:0", active: true }] }));
  const displayActive = vi.fn(async () => ({ activeDisplay: "sdl:0" }));
  const displayConfigGet = vi.fn(async () => ({ key: "default" }));
  const displayConfigSet = vi.fn(async () => ({ ok: true }));
  const displaySelect = vi.fn(async () => ({ restartRequired: true }));
  const handlers = createToolHandlers(
    makeInput({
      editorClient: {
        displayList,
        displayActive,
        displayConfigGet,
        displayConfigSet,
        displaySelect,
      },
    }),
  );

  await handlers.display_list({});
  await handlers.display_active({});
  await handlers.display_config_get({ key: "default" });
  await handlers.display_config_set({ key: "default", patch: "{\"preferences\":{\"uiFontScale\":1.2}}" });
  await handlers.display_select({ key: "sdl:0" });

  expect(displayList).toHaveBeenCalledOnce();
  expect(displayActive).toHaveBeenCalledOnce();
  expect(displayConfigGet).toHaveBeenCalledWith("default");
  expect(displayConfigSet).toHaveBeenCalledWith({
    key: "default",
    patch: "{\"preferences\":{\"uiFontScale\":1.2}}",
  });
  expect(displaySelect).toHaveBeenCalledWith("sdl:0");
});
```

Update the accepted tool surface expected list with:

```ts
"display_active",
"display_config_get",
"display_config_set",
"display_list",
"display_select",
```

- [ ] **Step 2: Run manager tests and verify failure**

Run:

```bash
cd tools/lxe_manager
npm test -- --run tests/mcp-server.test.ts
```

Expected: TypeScript compile errors for missing display client surface methods or missing handlers.

- [ ] **Step 3: Add editor client methods**

In `tools/lxe_manager/src/editor/editor-client.ts`, add:

```ts
displayList(): Promise<unknown> {
  return this.getJson("/api/display/list");
}

displayActive(): Promise<unknown> {
  return this.getJson("/api/display/active");
}

displayConfigGet(key: string): Promise<unknown> {
  return this.getJson(`/api/display/config?key=${encodeURIComponent(key)}`);
}

displayConfigSet(input: { key: string; patch: string }): Promise<unknown> {
  return this.postJson("/api/display/config", input);
}

displaySelect(key: string): Promise<unknown> {
  return this.postJson("/api/display/select", { key });
}
```

- [ ] **Step 4: Add manager MCP handlers**

In `tools/lxe_manager/src/mcp/server.ts`, extend `EditorClientSurface`:

```ts
displayList: () => Promise<unknown>;
displayActive: () => Promise<unknown>;
displayConfigGet: (key: string) => Promise<unknown>;
displayConfigSet: (input: { key: string; patch: string }) => Promise<unknown>;
displaySelect: (key: string) => Promise<unknown>;
```

Add handlers:

```ts
display_list: async () =>
  withEditorClient((editorClient) => editorClient.displayList()),
display_active: async () =>
  withEditorClient((editorClient) => editorClient.displayActive()),
display_config_get: async (args) =>
  withEditorClient((editorClient) =>
    editorClient.displayConfigGet(optionalString(args, "key") ?? "active"),
  ),
display_config_set: async (args) =>
  withEditorClient((editorClient) =>
    editorClient.displayConfigSet({
      key: readString(args, "key"),
      patch: readString(args, "patch"),
    }),
  ),
display_select: async (args) =>
  withEditorClient((editorClient) =>
    editorClient.displaySelect(readString(args, "key")),
  ),
```

- [ ] **Step 5: Run manager MCP tests**

Run:

```bash
cd tools/lxe_manager
npm test -- --run tests/mcp-server.test.ts
```

Expected: test passes.

- [ ] **Step 6: Commit**

```bash
git add tools/lxe_manager/src/editor/editor-client.ts tools/lxe_manager/src/mcp/server.ts tools/lxe_manager/tests/mcp-server.test.ts
git commit -m "feat: expose display config MCP tools"
```

## Task 7: Manager MCP Self-Restart

**Files:**
- Create: `tools/lxe_manager/src/ops/manager-ops.ts`
- Modify: `tools/lxe_manager/src/mcp/server.ts`
- Modify: `tools/lxe_manager/src/index.ts`
- Modify: `scripts/lxe_manager/start_mcp.sh`
- Modify: `scripts/lxe_manager/start_mcp.ps1`
- Test: `tools/lxe_manager/tests/manager-ops.test.ts`
- Test: `tools/lxe_manager/tests/mcp-server.test.ts`

- [ ] **Step 1: Write failing manager ops tests**

Create `tools/lxe_manager/tests/manager-ops.test.ts`:

```ts
import { describe, expect, it, vi } from "vitest";
import { ManagerOps, MANAGER_RESTART_EXIT_CODE } from "../src/ops/manager-ops.js";

describe("manager ops", () => {
  it("schedules a restart after returning an accepted response", async () => {
    const exit = vi.fn();
    const setTimeoutFn = vi.fn((callback: () => void) => {
      callback();
      return 0 as unknown as NodeJS.Timeout;
    });
    const ops = new ManagerOps({
      exit,
      setTimeout: setTimeoutFn,
      restartDelayMs: 5,
    });

    await expect(ops.restart()).resolves.toEqual({
      accepted: true,
      message: "manager restart scheduled; reconnect to the MCP endpoint",
      exitCode: MANAGER_RESTART_EXIT_CODE,
    });
    expect(setTimeoutFn).toHaveBeenCalledWith(expect.any(Function), 5);
    expect(exit).toHaveBeenCalledWith(MANAGER_RESTART_EXIT_CODE);
  });
});
```

- [ ] **Step 2: Add failing MCP route test**

In `mcp-server.test.ts`, extend `makeInput` with `managerOps` and add:

```ts
it("routes manager restart to manager ops", async () => {
  const restart = vi.fn(async () => ({ accepted: true }));
  const handlers = createToolHandlers(makeInput({ managerOps: { restart } }));

  await expect(handlers["ops.manager_restart"]({})).resolves.toEqual({
    content: [{ type: "text", text: "{\"accepted\":true}" }],
  });
  expect(restart).toHaveBeenCalledOnce();
});
```

Add `"ops.manager_restart"` to the accepted tool surface list.

- [ ] **Step 3: Run tests and verify failure**

Run:

```bash
cd tools/lxe_manager
npm test -- --run tests/manager-ops.test.ts tests/mcp-server.test.ts
```

Expected: missing `ManagerOps` and `managerOps` handler errors.

- [ ] **Step 4: Implement manager ops**

Create `tools/lxe_manager/src/ops/manager-ops.ts`:

```ts
export const MANAGER_RESTART_EXIT_CODE = 75;

export interface ManagerRestartResult {
  accepted: true;
  message: string;
  exitCode: number;
}

interface ManagerOpsOptions {
  exit?: (code: number) => never | void;
  setTimeout?: (callback: () => void, delayMs: number) => unknown;
  restartDelayMs?: number;
}

export class ManagerOps {
  private readonly exit: (code: number) => never | void;
  private readonly setTimeoutFn: (callback: () => void, delayMs: number) => unknown;
  private readonly restartDelayMs: number;

  constructor(options: ManagerOpsOptions = {}) {
    this.exit = options.exit ?? ((code) => process.exit(code));
    this.setTimeoutFn = options.setTimeout ?? setTimeout;
    this.restartDelayMs = options.restartDelayMs ?? 100;
  }

  async restart(): Promise<ManagerRestartResult> {
    this.setTimeoutFn(() => {
      this.exit(MANAGER_RESTART_EXIT_CODE);
    }, this.restartDelayMs);
    return {
      accepted: true,
      message: "manager restart scheduled; reconnect to the MCP endpoint",
      exitCode: MANAGER_RESTART_EXIT_CODE,
    };
  }
}
```

- [ ] **Step 5: Wire manager MCP handler**

In `server.ts`, add:

```ts
interface ManagerOpsSurface {
  restart: () => Promise<unknown>;
}
```

Extend `createToolHandlers` input:

```ts
managerOps?: ManagerOpsSurface;
```

Add handler:

```ts
"ops.manager_restart": async () => {
  if (!input.managerOps) {
    return errorText("manager_restart_unavailable", "manager restart is unavailable");
  }
  return jsonText(await input.managerOps.restart());
},
```

In `index.ts`, import and wire:

```ts
import { ManagerOps } from "./ops/manager-ops.js";

managerOps: new ManagerOps(),
```

- [ ] **Step 6: Add restart loop in shell script**

Modify `scripts/lxe_manager/start_mcp.sh`:

```bash
restart_code=75
while true; do
  node --import tsx ./src/index.ts "$@"
  code=$?
  if [ "$code" -ne "$restart_code" ]; then
    exit "$code"
  fi
  echo "lxe_manager MCP restarting after ops.manager_restart" >&2
done
```

Do not use `exec` inside the loop.

- [ ] **Step 7: Add restart loop in PowerShell script**

Modify `scripts/lxe_manager/start_mcp.ps1`:

```powershell
$RestartCode = 75
Push-Location $ManagerDir
try {
    while ($true) {
        & node --import tsx ./src/index.ts @ManagerArgs
        $ExitCode = $LASTEXITCODE
        if ($ExitCode -ne $RestartCode) {
            exit $ExitCode
        }
        Write-Error "lxe_manager MCP restarting after ops.manager_restart"
    }
}
finally {
    Pop-Location
}
```

- [ ] **Step 8: Run manager tests**

Run:

```bash
cd tools/lxe_manager
npm test -- --run tests/manager-ops.test.ts tests/mcp-server.test.ts
```

Expected: tests pass.

- [ ] **Step 9: Commit**

```bash
git add tools/lxe_manager/src/ops/manager-ops.ts tools/lxe_manager/src/mcp/server.ts tools/lxe_manager/src/index.ts tools/lxe_manager/tests/manager-ops.test.ts tools/lxe_manager/tests/mcp-server.test.ts scripts/lxe_manager/start_mcp.sh scripts/lxe_manager/start_mcp.ps1
git commit -m "feat: add manager MCP self restart"
```

## Task 8: Documentation And Final Verification

**Files:**
- Modify: `src/demos/lxe_editor/README.md`
- Modify: `notes/tools/lxe-manager-mcp.md`

- [ ] **Step 1: Update editor README**

In `src/demos/lxe_editor/README.md`, add display CLI examples under run/API docs:

````md
### Display selection

```sh
./build/src/demos/lxe_editor/lxe_editor --display-list
./build/src/demos/lxe_editor/lxe_editor --display 0
./build/src/demos/lxe_editor/lxe_editor --display "sdl:1:DELL U2720Q:3840x2160:1.50"
```

`data/lxe_editor/editor_config.yaml` uses `displayDefault` plus per-display
overrides. `activeDisplay` can be edited by hand; command-line `--display`
overrides it for that launch and updates it on save.
````

Add display tools and `ops.manager_restart` to the MCP surface list.

- [ ] **Step 2: Update manager MCP notes**

In `notes/tools/lxe-manager-mcp.md`, add rows:

```md
| `display_list` / `display_active` | 读取 editor display profile 列表和当前启动绑定 display |
| `display_config_get` / `display_config_set` / `display_select` | 读取、修改 display default/override，并设置下次启动 display |
| `ops.manager_restart` | 重启 manager MCP 服务本身，用于 `ops.repo_pull` 后应用 manager tool 变更 |
```

Add the remote update flow:

```md
manager 代码变更后：
1. `ops.repo_pull`
2. `ops.manager_restart`
3. 重新连接 MCP endpoint
4. 再执行 build/editor/display 验证
```

- [ ] **Step 3: Run full targeted verification**

Run:

```bash
cmake --build build --target lxe_editor test_lxe_editor_display_config test_lxe_editor_layout test_lxe_editor_api_service
ctest --test-dir build --output-on-failure -R "test_lxe_editor_display_config|test_lxe_editor_layout|test_lxe_editor_api_service"
cd tools/lxe_manager
npm test -- --run tests/mcp-server.test.ts tests/manager-ops.test.ts tests/editor-client.test.ts
npm run build
```

Expected:

- CMake builds all named targets.
- CTest reports all three C++ tests passed.
- Vitest reports all selected manager tests passed.
- TypeScript build exits 0.

- [ ] **Step 4: Manual smoke**

Run:

```bash
./build/src/demos/lxe_editor/lxe_editor --display-list
```

Expected: display list prints and exits before Vulkan renderer creation.

If an MCP manager is running through `scripts/lxe_manager/start_mcp.sh`, call `ops.manager_restart` from the MCP client and verify the server reconnects on the same endpoint. If no MCP client is attached, skip this manual step and rely on `manager-ops.test.ts`.

- [ ] **Step 5: Commit docs and verification fixes**

```bash
git add src/demos/lxe_editor/README.md notes/tools/lxe-manager-mcp.md
git commit -m "docs: document display profiles and manager restart"
```

## Self-Review

- Spec coverage: Tasks 1-2 cover display identity and backend enumeration. Tasks 3-4 cover v2 config, default/override model, migration, startup selection, and display list. Tasks 5-6 cover command/API/MCP display config. Task 7 covers manager MCP self-restart. Task 8 covers docs and verification.
- Placeholder scan: No placeholder markers or open-ended "add tests" instructions remain; each task includes target files, commands, and expected outcomes.
- Type consistency: Display helpers use `LX_core::DisplayInfo`; editor config APIs use `EditorDisplayConfigDocument`; manager restart uses `ops.manager_restart` and `MANAGER_RESTART_EXIT_CODE = 75` consistently.
