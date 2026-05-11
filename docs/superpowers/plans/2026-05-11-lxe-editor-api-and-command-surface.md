# Scene Viewer Automation And Command Surface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix scene-view picking and toolbar behavior, expose all key lxe_editor editor actions through the command system, and add a reusable HTTP/WebSocket API surface with token auth and test coverage.

**Architecture:** Split the work into four seams. First, tighten the existing lxe_editor interaction path so picking, hit markers, and toolbar actions behave correctly and are fully command-backed. Second, expand the command surface and structured probe outputs so every important editor behavior and state observation has a console command. Third, introduce a protocol-agnostic `LxeEditorApiService` that owns command execution, snapshots, and events without owning editor state. Fourth, add transport adapters for HTTP/WebSocket plus out-of-process integration tests, while keeping MCP as a future adapter over the same service.

**Tech Stack:** C++20, existing `CommandBus`/`EditorState`/`SceneViewerSession`, lxe_editor demo code, yaml-cpp, ImGui, existing test binaries under `src/test/integration`, and a lightweight HTTP/WebSocket server library already acceptable to the repo or added narrowly to `lxe_editor`.

---

## File Structure

- Modify: `src/demos/lxe_editor/ui_overlay.hpp`
  Responsibility: declare toolbar callbacks or state access needed for icon-only toolbar and reset-camera action.
- Modify: `src/demos/lxe_editor/ui_overlay.cpp`
  Responsibility: remove static toolbar text, add reset icon button, keep tooltip-only UX, and route button actions through command-backed callbacks.
- Modify: `src/demos/lxe_editor/scene_interaction_controller.hpp`
  Responsibility: expose scene-view-rect aware picking entry points and keep selection debug state queryable.
- Modify: `src/demos/lxe_editor/scene_interaction_controller.cpp`
  Responsibility: compute scene-view-local ray inputs, fix hit-point placement, and keep AABB/marker debug draw in sync with selection.
- Create: `src/demos/lxe_editor/scene_view_rect.hpp`
  Responsibility: small value type for the effective visible scene rect and hit-testing helpers.
- Create: `src/demos/lxe_editor/scene_view_rect.cpp`
  Responsibility: derive the visible scene rect from current editor layout/window state.
- Modify: `src/demos/lxe_editor/main.cpp`
  Responsibility: wire scene-view rect into update-hook picking, bind new commands, and host api-service/server lifecycle.
- Modify: `src/demos/lxe_editor/camera_rig.hpp`
  Responsibility: expose a narrow re-sync API for “editor cam <- game cam” without changing mode semantics.
- Modify: `src/demos/lxe_editor/camera_rig.cpp`
  Responsibility: implement controller-state resync from the current camera pose.
- Modify: `src/core/editor/commands/builtin_commands.cpp`
  Responsibility: add new editor commands and structured probe commands, using shared helpers rather than UI-only paths.
- Create: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
  Responsibility: declare protocol-agnostic API for command execution, snapshots, and events.
- Create: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
  Responsibility: implement command-backed actions, state snapshots, event generation, and error translation.
- Create: `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
  Responsibility: shared JSON-ish request/response/event schema structs reused by HTTP, WebSocket, and future MCP adapters.
- Create: `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
  Responsibility: serialization helpers and stable schema utilities.
- Create: `src/demos/lxe_editor/lxe_editor_api_server.hpp`
  Responsibility: declare HTTP/WebSocket server wrapper, config, and auth contract.
- Create: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
  Responsibility: implement socket lifecycle, request routing, token auth, and event broadcast over HTTP/WebSocket.
- Create: `src/demos/lxe_editor/api_token_state.hpp`
  Responsibility: token file path/config helpers.
- Create: `src/demos/lxe_editor/api_token_state.cpp`
  Responsibility: load-or-generate token file handling under `data/lxe_editor/`.
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
  Responsibility: compile the new api and scene-rect modules plus any narrowly scoped transport dependency.
- Modify: `src/demos/lxe_editor/README.md`
  Responsibility: document toolbar behavior, new commands, token/auth files, and HTTP/WebSocket usage.
- Modify: `notes/subsystems/scene.md`
  Responsibility: update subsystem notes for command-first api and lxe_editor testing hooks.
- Modify: `src/test/CMakeLists.txt`
  Responsibility: add sources/targets for new lxe_editor API tests and any transport dependency linkage.
- Modify: `src/test/integration/test_lxe_editor_interaction.cpp`
  Responsibility: cover scene-view rect picking, corrected hit markers, and reset-camera behavior.
- Modify: `src/test/integration/test_lxe_editor_layout.cpp`
  Responsibility: cover icon-only toolbar and toolbar layout/persistence behavior after text removal.
- Modify: `src/test/integration/test_command_bus.cpp`
  Responsibility: cover new mode/reset/probe commands and structured JSON outputs.
- Create: `src/test/integration/test_lxe_lxe_editor_api_service.cpp`
  Responsibility: cover protocol-agnostic snapshots, events, and command-backed api actions without opening sockets.
- Create: `src/test/integration/test_lxe_lxe_editor_api_server.cpp`
  Responsibility: launch a real lxe_editor child process, authenticate over HTTP/WebSocket, drive commands, and assert state/events.

## Task 1: Add failing coverage for scene-view rect picking and toolbar reset UX

**Files:**
- Create: `src/demos/lxe_editor/scene_view_rect.hpp`
- Create: `src/demos/lxe_editor/scene_view_rect.cpp`
- Modify: `src/test/integration/test_lxe_editor_interaction.cpp`
- Modify: `src/test/integration/test_lxe_editor_layout.cpp`

- [ ] **Step 1: Add a failing scene-view-rect picking test**

Extend `src/test/integration/test_lxe_editor_interaction.cpp` with a focused test that simulates a visible scene rect smaller than the full window:

```cpp
void testSelectionPickingUsesSceneViewRectInsteadOfWholeWindow() {
  Fixture fixture;
  const LX_demo::lxe_editor::SceneViewRect rect{
      .x = 280.0f,
      .y = 68.0f,
      .width = 640.0f,
      .height = 432.0f,
  };

  const auto result = fixture.controller.dispatchPickingClick(
      {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f}, rect);
  EXPECT(result.ok, "scene-view-local center click should succeed");
  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1 && selected.front() == fixture.targetNode,
         "scene-view-local picking should still hit the centered mesh");
}
```

- [ ] **Step 2: Add a failing miss-outside-rect test**

Add a second test to the same file:

```cpp
void testSelectionPickingIgnoresClicksOutsideSceneViewRect() {
  Fixture fixture;
  const LX_demo::lxe_editor::SceneViewRect rect{
      .x = 280.0f,
      .y = 68.0f,
      .width = 640.0f,
      .height = 432.0f,
  };

  const auto result = fixture.controller.dispatchPickingClick({32.0f, 32.0f}, rect);
  EXPECT(!result.ok, "clicks outside the scene view rect should not dispatch scene picks");
  EXPECT(fixture.editorState.getSelected().empty(),
         "outside-rect clicks should leave selection untouched");
}
```

- [ ] **Step 3: Add a failing reset-toolbar layout test**

Extend `src/test/integration/test_lxe_editor_layout.cpp` with a toolbar assertion:

```cpp
void testToolbarRendersIconOnlyWithoutStaticModeText() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] toolbar icon-only test\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  ImGui::NewFrame();
  harness.ui.drawFrame();
  ImGuiWindow* toolbar = ImGui::FindWindowByName("Toolbar");
  EXPECT(toolbar != nullptr, "toolbar should exist");
  if (toolbar) {
    const std::string text(toolbar->DC.TextBuffer.begin(), toolbar->DC.TextBuffer.end());
    EXPECT(text.find("Selection") == std::string::npos,
           "toolbar should not render static mode text");
    EXPECT(text.find("Preview") == std::string::npos,
           "toolbar should not render preview label text");
  }
  ImGui::EndFrame();
  ImGui::DestroyContext();
}
```

- [ ] **Step 4: Add a failing reset-camera behavior test**

Add another focused interaction test:

```cpp
void testResetEditorCameraToGameCameraCopiesPoseWithoutPreviewToggle() {
  Fixture fixture;
  fixture.editorCameraNode->setTranslation({3.0f, 4.0f, 5.0f});
  fixture.gameCameraNode->setTranslation({10.0f, 20.0f, 30.0f});

  const auto result = fixture.bus.dispatch("cam reset-editor-to-game");
  EXPECT(result.ok, "reset-editor-to-game command should succeed");
  EXPECT(fixture.editorCameraNode->getTranslation() ==
             fixture.gameCameraNode->getTranslation(),
         "editor camera should copy game camera translation");
  EXPECT(!fixture.editorState.isPreviewEnabled(),
         "reset-editor-to-game should not toggle preview");
}
```

- [ ] **Step 5: Run the focused red tests**

Run:

```bash
cmake --build build --target test_lxe_editor_interaction test_lxe_editor_layout test_command_bus
./build/src/test/test_lxe_editor_interaction
./build/src/test/test_lxe_editor_layout
./build/src/test/test_command_bus
```

Expected: FAIL because `lxe_editor` still uses full-window coordinates, the toolbar still renders static text, and the reset command does not exist yet.

- [ ] **Step 6: Commit the red coverage**

```bash
git add src/test/integration/test_lxe_editor_interaction.cpp src/test/integration/test_lxe_editor_layout.cpp src/test/integration/test_command_bus.cpp
git commit -m "test: add scene viewer rect and toolbar reset coverage"
```

## Task 2: Fix scene-view picking and make the toolbar icon-only

**Files:**
- Create: `src/demos/lxe_editor/scene_view_rect.hpp`
- Create: `src/demos/lxe_editor/scene_view_rect.cpp`
- Modify: `src/demos/lxe_editor/scene_interaction_controller.hpp`
- Modify: `src/demos/lxe_editor/scene_interaction_controller.cpp`
- Modify: `src/demos/lxe_editor/ui_overlay.hpp`
- Modify: `src/demos/lxe_editor/ui_overlay.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Test: `src/test/integration/test_lxe_editor_interaction.cpp`
- Test: `src/test/integration/test_lxe_editor_layout.cpp`

- [ ] **Step 1: Define a focused scene-view rect type**

Create `src/demos/lxe_editor/scene_view_rect.hpp`:

```cpp
#pragma once

#include "core/math/vec.hpp"

#include <optional>

namespace LX_demo::lxe_editor {

struct SceneViewRect final {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  [[nodiscard]] bool isValid() const;
  [[nodiscard]] bool contains(const LX_core::Vec2f& pixel) const;
  [[nodiscard]] LX_core::Vec2f localPixel(const LX_core::Vec2f& pixel) const;
  [[nodiscard]] LX_core::Vec2f size() const;
};

[[nodiscard]] SceneViewRect makeSceneViewRect(
    float windowWidth, float windowHeight,
    float leftInset, float topInset, float rightInset, float bottomInset);

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 2: Implement rect validation and local-coordinate helpers**

Create `src/demos/lxe_editor/scene_view_rect.cpp`:

```cpp
#include "demos/lxe_editor/scene_view_rect.hpp"

#include <algorithm>

namespace LX_demo::lxe_editor {

bool SceneViewRect::isValid() const {
  return width > 1.0f && height > 1.0f;
}

bool SceneViewRect::contains(const LX_core::Vec2f& pixel) const {
  return isValid() && pixel.x >= x && pixel.y >= y &&
         pixel.x < x + width && pixel.y < y + height;
}

LX_core::Vec2f SceneViewRect::localPixel(const LX_core::Vec2f& pixel) const {
  return {pixel.x - x, pixel.y - y};
}

LX_core::Vec2f SceneViewRect::size() const {
  return {width, height};
}

SceneViewRect makeSceneViewRect(float windowWidth, float windowHeight,
                                float leftInset, float topInset,
                                float rightInset, float bottomInset) {
  SceneViewRect rect;
  rect.x = std::max(0.0f, leftInset);
  rect.y = std::max(0.0f, topInset);
  rect.width = std::max(0.0f, windowWidth - leftInset - rightInset);
  rect.height = std::max(0.0f, windowHeight - topInset - bottomInset);
  return rect;
}

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 3: Make scene picking rect-aware**

Update `scene_interaction_controller.hpp/.cpp` so the public API takes `SceneViewRect` instead of raw viewport size:

```cpp
[[nodiscard]] LX_core::CommandResult dispatchPickingClick(
    const LX_core::Vec2f& screenPixel, const SceneViewRect& sceneViewRect);
```

Implementation shape:

```cpp
if (!sceneViewRect.contains(screenPixel)) {
  return LX_core::CommandResult{false, "click outside scene view", {}, {}};
}

const LX_core::Vec2f localPixel = sceneViewRect.localPixel(screenPixel);
const LX_core::Ray ray =
    editorCamera->get().pickRay(localPixel, sceneViewRect.size());
```

Keep the existing AABB/hit-marker logic after this change.

- [ ] **Step 4: Compute and pass the effective scene rect from the main loop**

In `main.cpp`, derive the current main scene rect from the editor chrome. Use the same numeric defaults already embedded in `UiOverlay::drawFrame()` to keep behavior aligned:

```cpp
const float leftInset = 280.0f + 24.0f;
const float rightInset = 360.0f + 24.0f;
const float topInset = 68.0f;
const float bottomInset = 220.0f;
const demo::SceneViewRect sceneViewRect =
    demo::makeSceneViewRect(static_cast<float>(windowWidth),
                            static_cast<float>(windowHeight),
                            leftInset, topInset, rightInset, bottomInset);
```

Pass that rect into `updateSelectionMode(...)` / `dispatchPickingClick(...)`.

- [ ] **Step 5: Remove static toolbar text and add a callback slot for reset**

In `ui_overlay.hpp`, add a command or callback hook for reset, then in `ui_overlay.cpp`:

```cpp
ImGui::SameLine();
if (drawIconToggleButton("##tool_reset_editor_cam", false,
                         "Reset Editor Camera To Game Camera",
                         drawResetCameraIcon) && m_commandBus) {
  (void)m_commandBus->get().dispatch("cam reset-editor-to-game");
}
```

Delete the static `ImGui::TextUnformatted(editModeLabel(...))` and preview text branch at the end of `drawToolbarPanel()`.

- [ ] **Step 6: Rebuild and rerun the green tests**

Run:

```bash
cmake --build build --target lxe_editor test_lxe_editor_interaction test_lxe_editor_layout test_command_bus
./build/src/test/test_lxe_editor_interaction
./build/src/test/test_lxe_editor_layout
./build/src/test/test_command_bus
```

Expected: The new picking and toolbar tests pass, while later API tests still do not exist yet.

- [ ] **Step 7: Commit the interaction-layer fix**

```bash
git add src/demos/lxe_editor/scene_view_rect.hpp src/demos/lxe_editor/scene_view_rect.cpp src/demos/lxe_editor/scene_interaction_controller.hpp src/demos/lxe_editor/scene_interaction_controller.cpp src/demos/lxe_editor/ui_overlay.hpp src/demos/lxe_editor/ui_overlay.cpp src/demos/lxe_editor/main.cpp src/test/integration/test_lxe_editor_interaction.cpp src/test/integration/test_lxe_editor_layout.cpp
git commit -m "feat: fix scene viewer picking and toolbar icons"
```

## Task 3: Expand the command surface and probe outputs

**Files:**
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/demos/lxe_editor/camera_rig.hpp`
- Modify: `src/demos/lxe_editor/camera_rig.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Add failing command-bus coverage for mode/reset/probe commands**

Extend `test_command_bus.cpp` with coverage like:

```cpp
void testSceneViewerModeAndProbeCommands() {
  SceneViewerFixture fixture;

  EXPECT(fixture.bus.dispatch("mode selection").ok, "mode selection should succeed");
  EXPECT(fixture.bus.dispatch("mode orbit").ok, "mode orbit should succeed");
  EXPECT(fixture.bus.dispatch("mode freefly").ok, "mode freefly should succeed");

  const auto reset = fixture.bus.dispatch("cam reset-editor-to-game");
  EXPECT(reset.ok, "cam reset-editor-to-game should succeed");

  const auto summary = fixture.bus.dispatch("state summary");
  EXPECT(summary.ok, "state summary should succeed");
  EXPECT(summary.structured.find("\"previewEnabled\"") != std::string::npos,
         "state summary should return structured JSON");
}
```

- [ ] **Step 2: Add the rig resync API needed by reset**

In `camera_rig.hpp`:

```cpp
void resyncFromAttachedCamera();
```

In `camera_rig.cpp`:

```cpp
void CameraRig::resyncFromAttachedCamera() {
  if (!m_camera) {
    return;
  }
  syncOrbitFromCamera(m_orbit, m_camera->get());
  syncFreeFlyFromCamera(m_freefly, m_camera->get());
}
```

- [ ] **Step 3: Register command-backed lxe_editor actions**

In `main.cpp`, after builtin command registration, register narrow lxe_editor commands against the live `CommandBus`:

```cpp
m_commandBus->registerHandler("mode", "switch lxe_editor edit mode",
  [this](std::vector<std::string> args) -> LX_core::CommandResult {
    if (args.size() != 1) {
      return makeCommandError("usage: mode <selection|orbit|freefly>");
    }
    if (args[0] == "selection") {
      m_ui.setEditMode(demo::UiOverlay::EditMode::Selection);
    } else if (args[0] == "orbit") {
      m_ui.setEditMode(demo::UiOverlay::EditMode::Orbit);
    } else if (args[0] == "freefly") {
      m_ui.setEditMode(demo::UiOverlay::EditMode::FreeFly);
    } else {
      return makeCommandError("unknown mode");
    }
    return makeCommandOk("mode changed",
                         "{\"mode\":\"" + jsonEscape(args[0]) + "\"}");
  });
```

Register `cam reset-editor-to-game` similarly by copying the gameplay camera transform to the editor camera and calling `m_rig.resyncFromAttachedCamera()`.

- [ ] **Step 4: Add probe/state commands with structured JSON**

Still in `main.cpp` or a lxe_editor-specific command registrar, add:

```cpp
"state summary"
"state selection"
"state cameras"
"state scene"
"state toolbar"
"pick"
"debug overlay"
```

Each handler should return stable JSON keys. Example shape for `state selection`:

```json
{
  "selected": ["/cube"],
  "primary": "/cube",
  "worldAabb": {"min": {...}, "max": {...}},
  "lastHitPoint": {"x": 0.0, "y": 0.0, "z": 0.0}
}
```

- [ ] **Step 5: Run the command-surface tests**

Run:

```bash
cmake --build build --target test_command_bus
./build/src/test/test_command_bus
```

Expected: PASS for new mode/reset/probe commands, with structured JSON fields present.

- [ ] **Step 6: Commit the command expansion**

```bash
git add src/demos/lxe_editor/camera_rig.hpp src/demos/lxe_editor/camera_rig.cpp src/demos/lxe_editor/main.cpp src/core/editor/commands/builtin_commands.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: add scene viewer mode and probe commands"
```

## Task 4: Add a protocol-agnostic API service

**Files:**
- Create: `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
- Create: `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
- Create: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- Create: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Create: `src/test/integration/test_lxe_lxe_editor_api_service.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add failing service-level snapshot and event tests**

Create `test_lxe_lxe_editor_api_service.cpp` with focused coverage:

```cpp
void testAutomationServiceExecutesCommandAndReturnsSnapshot() {
  Fixture fixture;
  const auto result = fixture.service.executeCommand("scene list");
  EXPECT(result.ok, "API service should execute commands");

  const auto summary = fixture.service.summaryState();
  EXPECT(summary.previewEnabled == false, "summary snapshot should expose preview flag");
  EXPECT(summary.editMode == "orbit", "summary snapshot should expose edit mode");
}

void testAutomationServicePublishesSelectionChangedEvent() {
  Fixture fixture;
  (void)fixture.service.executeCommand("select /cube");
  const auto events = fixture.service.drainEvents();
  EXPECT(!events.empty(), "selection command should produce events");
  EXPECT(events.back().type == "selection.changed",
         "selection change should be published as an api event");
}
```

- [ ] **Step 2: Define protocol structs instead of transport-specific JSON blobs**

Create `lxe_editor_api_protocol.hpp` with narrow domain types:

```cpp
struct ApiCommandResult final {
  bool ok = false;
  std::string message;
  std::string structuredJson;
};

struct ApiEvent final {
  std::string type;
  u64 seq = 0;
  std::string payloadJson;
};

struct ApiSummaryState final {
  bool previewEnabled = false;
  std::string editMode;
  std::string scenePath;
  bool dirty = false;
};
```

Add helper serializers in the `.cpp` file so HTTP/WebSocket/MCP can reuse them later.

- [ ] **Step 3: Implement `LxeEditorApiService` over references**

Create `lxe_editor_api_service.hpp/.cpp`:

```cpp
class LxeEditorApiService final {
public:
  LxeEditorApiService(LX_core::CommandBus& commandBus,
                          LX_core::EditorState& editorState,
                          SceneViewerSession& session,
                          UiOverlay& ui);

  [[nodiscard]] ApiCommandResult executeCommand(std::string_view line);
  [[nodiscard]] std::string stateSummaryJson() const;
  [[nodiscard]] std::string stateSelectionJson() const;
  [[nodiscard]] std::string stateCamerasJson() const;
  [[nodiscard]] std::string stateSceneJson() const;
  [[nodiscard]] std::string stateToolbarJson() const;
  [[nodiscard]] std::vector<ApiEvent> drainEvents();
  void publishStateChangedEvents();
};
```

Inside `executeCommand`, call the existing command bus and append a `command.executed` event. Inside `publishStateChangedEvents`, diff current snapshots against the previous frame and emit `selection.changed`, `mode.changed`, `preview.changed`, and `dirty.changed` when they change.

- [ ] **Step 4: Wire the service into `lxe_editor` lifecycle**

In `main.cpp`, construct `LxeEditorApiService` after the session/command bus/UI are bound, and call a per-frame publish hook after update logic:

```cpp
if (m_apiService) {
  m_apiService->publishStateChangedEvents();
}
```

Keep the service protocol-agnostic. Do not open sockets in this task.

- [ ] **Step 5: Run the service-level tests**

Run:

```bash
cmake --build build --target test_lxe_lxe_editor_api_service
./build/src/test/test_lxe_lxe_editor_api_service
```

Expected: PASS for snapshots and event generation with no server involved.

- [ ] **Step 6: Commit the API core**

```bash
git add src/demos/lxe_editor/lxe_editor_api_protocol.hpp src/demos/lxe_editor/lxe_editor_api_protocol.cpp src/demos/lxe_editor/lxe_editor_api_service.hpp src/demos/lxe_editor/lxe_editor_api_service.cpp src/demos/lxe_editor/main.cpp src/test/integration/test_lxe_lxe_editor_api_service.cpp src/test/CMakeLists.txt
git commit -m "feat: add scene viewer API service"
```

## Task 5: Add token state and HTTP/WebSocket transport

**Files:**
- Create: `src/demos/lxe_editor/api_token_state.hpp`
- Create: `src/demos/lxe_editor/api_token_state.cpp`
- Create: `src/demos/lxe_editor/lxe_editor_api_server.hpp`
- Create: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/demos/lxe_editor/README.md`

- [ ] **Step 1: Add failing auth/token tests around load-or-generate behavior**

Before server implementation, add a focused local test (either to the new API service test or a tiny new test source) like:

```cpp
void testApiTokenStateGeneratesMissingTokenFile() {
  const auto root = std::filesystem::temp_directory_path() / "lx_api_token";
  std::filesystem::remove_all(root);
  const auto token = loadOrCreateAutomationToken(root / "api_token.txt");
  EXPECT(!token.empty(), "generated token should not be empty");
  EXPECT(std::filesystem::exists(root / "api_token.txt"),
         "token file should be written on first run");
}
```

- [ ] **Step 2: Implement narrow token-file helpers**

Create `api_token_state.hpp/.cpp`:

```cpp
[[nodiscard]] std::string loadOrCreateAutomationToken(
    const std::filesystem::path& tokenPath);
```

Implementation requirements:

- create parent directories
- reuse existing token file if present
- otherwise generate a random printable token
- never print the token value to logs

- [ ] **Step 3: Implement HTTP/WebSocket server wrapper**

Create `lxe_editor_api_server.hpp/.cpp` with a transport-agnostic constructor but concrete HTTP/WS routing:

```cpp
struct LxeEditorApiServerConfig final {
  std::string host = "0.0.0.0";
  u16 port = 4599;
  std::filesystem::path tokenFile;
};

class LxeEditorApiServer final {
public:
  LxeEditorApiServer(LxeEditorApiService& service,
                         LxeEditorApiServerConfig config);

  void start();
  void stop();
  void pump();
};
```

Required routes:

- `POST /api/command`
- `GET /api/state/summary`
- `GET /api/state/selection`
- `GET /api/state/cameras`
- `GET /api/state/scene`
- `GET /api/state/toolbar`
- `POST /api/mode`
- `POST /api/preview`
- `POST /api/camera/reset-editor-to-game`
- `POST /api/pick`

Authentication rule:

- reject missing/invalid bearer token with `401`
- apply the same token requirement to WebSocket handshake

- [ ] **Step 4: Wire startup flags and server lifecycle into `main.cpp`**

Add startup parsing for:

```text
--api-enable
--api-host <host>
--api-port <port>
--api-token-file <path>
```

Then:

```cpp
const std::string token =
    demo::loadOrCreateAutomationToken(resolveRuntimePath("data/lxe_editor/api_token.txt"));
demo::LxeEditorApiServer server(*apiService, serverConfig);
server.start();
```

Call `server.pump()` during the main update loop if the chosen transport library requires polling, and `server.stop()` on shutdown.

- [ ] **Step 5: Update README with auth and endpoint usage**

Document:

- default host/port
- token file path
- `Authorization: Bearer ...`
- curl examples for `scene list`, `mode orbit`, `cam reset-editor-to-game`
- WebSocket event flow at a high level

- [ ] **Step 6: Rebuild the demo and smoke-test startup**

Run:

```bash
cmake --build build --target lxe_editor
timeout 8s xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor --api-enable
```

Expected: process starts, binds API server, writes/reads token file, and exits only because `timeout` stops the smoke test.

- [ ] **Step 7: Commit the transport layer**

```bash
git add src/demos/lxe_editor/api_token_state.hpp src/demos/lxe_editor/api_token_state.cpp src/demos/lxe_editor/lxe_editor_api_server.hpp src/demos/lxe_editor/lxe_editor_api_server.cpp src/demos/lxe_editor/CMakeLists.txt src/demos/lxe_editor/main.cpp src/demos/lxe_editor/README.md
git commit -m "feat: add scene viewer API server"
```

## Task 6: Add out-of-process API tests

**Files:**
- Create: `src/test/integration/test_lxe_lxe_editor_api_server.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add a failing child-process API test**

Create `test_lxe_lxe_editor_api_server.cpp` that:

```cpp
void testAutomationServerAcceptsAuthenticatedCommands() {
  const ChildProcess proc = launchSceneViewerWithAutomation();
  const std::string token = readTokenFile(proc.tokenFile);
  const HttpResponse response = httpPostJson(
      proc.baseUrl + "/api/command",
      R"({"line":"scene list"})",
      {{"Authorization", "Bearer " + token}});
  EXPECT(response.status == 200, "authenticated command should succeed");
  EXPECT(response.body.find("\"ok\":true") != std::string::npos,
         "scene list response should report success");
}
```

- [ ] **Step 2: Add a failing unauthorized-path test**

In the same file:

```cpp
void testAutomationServerRejectsMissingToken() {
  const ChildProcess proc = launchSceneViewerWithAutomation();
  const HttpResponse response = httpGet(proc.baseUrl + "/api/state/summary");
  EXPECT(response.status == 401, "missing bearer token should be rejected");
}
```

- [ ] **Step 3: Add a failing WebSocket event test**

Add a minimal event-stream test:

```cpp
void testAutomationWebSocketPublishesSelectionChanged() {
  const ChildProcess proc = launchSceneViewerWithAutomation();
  const std::string token = readTokenFile(proc.tokenFile);
  WebSocketClient ws = connectAutomationWebSocket(proc.wsUrl, token);
  (void)httpPostJson(proc.baseUrl + "/api/command",
                     R"({"line":"select /cube"})",
                     {{"Authorization", "Bearer " + token}});
  const std::string event = ws.readMessageWithTimeout();
  EXPECT(event.find("\"type\":\"selection.changed\"") != std::string::npos,
         "selection commands should publish websocket events");
}
```

- [ ] **Step 4: Implement the child-process helpers and make the tests pass**

In the same test file, add focused helpers:

```cpp
struct ChildProcess final {
  pid_t pid = -1;
  std::filesystem::path tokenFile;
  std::string baseUrl;
  std::string wsUrl;
};
```

Required behavior:

- launch `lxe_editor --api-enable --api-port <ephemeral>`
- wait until token file and HTTP ready state exist
- tear down child process reliably even on failure

- [ ] **Step 5: Run the full API test set**

Run:

```bash
cmake --build build --target test_lxe_lxe_editor_api_service test_lxe_lxe_editor_api_server lxe_editor
./build/src/test/test_lxe_lxe_editor_api_service
./build/src/test/test_lxe_lxe_editor_api_server
timeout 8s xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor --api-enable
```

Expected: PASS for in-process and out-of-process api coverage; the smoke test exits via `timeout` only.

- [ ] **Step 6: Commit the end-to-end api coverage**

```bash
git add src/test/integration/test_lxe_lxe_editor_api_server.cpp src/test/CMakeLists.txt
git commit -m "test: add scene viewer API server coverage"
```

## Task 7: Sync docs and final verification

**Files:**
- Modify: `notes/subsystems/scene.md`
- Modify: `src/demos/lxe_editor/README.md`

- [ ] **Step 1: Update subsystem notes for command-first api**

In `notes/subsystems/scene.md`, add or refresh the `lxe_editor` section so it explicitly states:

```md
- lxe_editor editor actions are command-first; HTTP/WebSocket api reuses the same command and snapshot surface.
- lxe_editor local API auth token lives under `data/lxe_editor/api_token.txt`.
- future MCP support is expected to adapt `LxeEditorApiService` rather than bypass it.
```

- [ ] **Step 2: Refresh README examples**

In `src/demos/lxe_editor/README.md`, add examples for:

```bash
TOKEN=$(cat data/lxe_editor/api_token.txt)
curl -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"line":"scene list"}' \
  http://127.0.0.1:4599/api/command
```

Also document the new commands:

- `mode selection`
- `mode orbit`
- `mode freefly`
- `cam reset-editor-to-game`
- `state summary`
- `state selection`
- `state cameras`
- `state scene`
- `state toolbar`
- `pick <x> <y>`
- `debug overlay`

- [ ] **Step 3: Run the final verification matrix**

Run:

```bash
cmake --build build --target lxe_editor test_command_bus test_lxe_editor_interaction test_lxe_editor_layout test_lxe_lxe_editor_api_service test_lxe_lxe_editor_api_server
./build/src/test/test_command_bus
./build/src/test/test_lxe_editor_interaction
./build/src/test/test_lxe_editor_layout
./build/src/test/test_lxe_lxe_editor_api_service
./build/src/test/test_lxe_lxe_editor_api_server
timeout 8s xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor --api-enable
```

Expected: all targeted tests pass; demo smoke starts cleanly and is only terminated by `timeout`.

- [ ] **Step 4: Commit the documentation sync**

```bash
git add notes/subsystems/scene.md src/demos/lxe_editor/README.md
git commit -m "docs: document scene viewer API surface"
```

## Self-Review

- Spec coverage:
  - scene-view rect fix: Task 1-2
  - icon-only toolbar and reset button: Task 1-3 and Task 2-3 to 2-5
  - command coverage and probes: Task 3
  - protocol-agnostic API core: Task 4
  - HTTP/WebSocket + token auth: Task 5
  - out-of-process testing: Task 6
  - MCP readiness through service/protocol separation: Task 4 + Task 7 docs
- Placeholder scan:
  - No `TODO` / `TBD` placeholders remain.
  - Every task names concrete files and verification commands.
- Type consistency:
  - `SceneViewRect`, `LxeEditorApiService`, `LxeEditorApiServer`, and `loadOrCreateAutomationToken(...)` are introduced before later tasks rely on them.
