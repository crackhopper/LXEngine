#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/input/key_code.hpp"
#include "core/input/mock_input_state.hpp"
#include "core/platform/window.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "demos/lxe_editor/camera_rig.hpp"
#include "demos/lxe_editor/editor_config_state.hpp"
#include "demos/lxe_editor/editor_data_state.hpp"
#include "demos/lxe_editor/ui_overlay.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

bool setupMinimalImGui() {
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280.0f, 720.0f);
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;
  unsigned char *pixels = nullptr;
  int w = 0;
  int h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  return pixels != nullptr && w > 0 && h > 0;
}

struct UiHarness final {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr targetNode;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::EditorConfigDocument config;
  LX_core::SceneTreePanel sceneTreePanel;
  LX_core::InspectorPanel inspectorPanel;
  LX_core::ConsolePanel consolePanel;
  LX_core::ViewportOverlay viewportOverlay;
  LX_demo::lxe_editor::UiOverlay ui;

  UiHarness()
      : scene(LX_core::Scene::create("layout_scene")),
        editorCameraNode(LX_core::SceneNode::create("editor_camera")),
        targetNode(LX_core::SceneNode::create("toolbar_target")),
        sceneTreePanel(bus, editorState, *scene),
        inspectorPanel(bus, editorState), consolePanel(bus),
        viewportOverlay(bus, editorState, *scene) {
    editorCameraNode->setName("editor_cam");
    targetNode->setName("toolbar_target");
    scene->addRenderable(targetNode);
    auto editorCamera =
        editorCameraNode->addComponent<LX_core::CameraComponent>();
    scene->addCamera(editorCameraNode);
    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(editorCameraNode);
    (void)editorState.syncActiveCamera(*scene);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
    rig.attach(editorCamera->get());
    ui.attach(rig, bus, editorState, config, viewportOverlay, sceneTreePanel,
              inspectorPanel, consolePanel);
  }
};

class StubWindow final : public LX_core::Window {
public:
  explicit StubWindow(const LX_core::WindowPlacement &placement)
      : m_placement(placement) {}

  int getWidth() const override { return m_placement.width; }
  int getHeight() const override { return m_placement.height; }
  void updateSize(bool *closed, int *width, int *height) override {
    if (closed) {
      *closed = false;
    }
    if (width) {
      *width = m_placement.width;
    }
    if (height) {
      *height = m_placement.height;
    }
  }
  void getRequiredExtensions(std::vector<const char *> &) const override {}
  LX_core::WindowGraphicsHandle
  createGraphicsHandle(GraphicsAPI,
                       LX_core::GraphicsInstanceHandle) const override {
    return nullptr;
  }
  void destroyGraphicsHandle(GraphicsAPI, LX_core::GraphicsInstanceHandle,
                             LX_core::WindowGraphicsHandle) const override {}
  LX_core::InputStateSharedPtr getInputState() const override {
    return nullptr;
  }
  void *getNativeHandle() const override { return nullptr; }
  void onClose(std::function<bool()>) override {}
  bool shouldClose() override { return false; }
  LX_core::WindowPlacement getPlacement() const override { return m_placement; }
  LX_core::WindowUsableBounds getUsableBounds() const override {
    return m_usableBounds;
  }
  LX_core::WindowUsableBounds getUsableBoundsForPlacement(
      const LX_core::WindowPlacement &placement) const override {
    (void)placement;
    return m_placementUsableBounds.value_or(m_usableBounds);
  }
  void applyPlacement(const LX_core::WindowPlacement &placement) override {
    m_placement = placement;
    m_appliedPlacement = placement;
  }

  void setUsableBounds(const LX_core::WindowUsableBounds &bounds) {
    m_usableBounds = bounds;
  }
  void setUsableBoundsForPlacement(const LX_core::WindowUsableBounds &bounds) {
    m_placementUsableBounds = bounds;
  }

  [[nodiscard]] const std::optional<LX_core::WindowPlacement> &
  appliedPlacement() const {
    return m_appliedPlacement;
  }

private:
  LX_core::WindowPlacement m_placement{};
  LX_core::WindowUsableBounds m_usableBounds{
      .x = 0, .y = 0, .width = 1920, .height = 1080};
  std::optional<LX_core::WindowUsableBounds> m_placementUsableBounds;
  std::optional<LX_core::WindowPlacement> m_appliedPlacement;
};

[[nodiscard]] std::string makePersistedEditorConfigYaml() {
  return "version: 1\n"
         "window:\n"
         "  x: 42\n"
         "  y: 64\n"
         "  width: 1440\n"
         "  height: 900\n"
         "  maximized: true\n"
         "layout:\n"
         "  windows:\n"
         "    - id: Toolbar\n"
         "      visible: true\n"
         "      collapsed: false\n"
         "      x: 24\n"
         "      y: 18\n"
         "      width: 260\n"
         "      height: 60\n"
         "    - id: Scene Tree\n"
         "      visible: true\n"
         "      collapsed: true\n"
         "      x: 96\n"
         "      y: 84\n"
         "      width: 312\n"
         "      height: 288\n"
         "    - id: Inspector\n"
         "      visible: true\n"
         "      collapsed: false\n"
         "      x: 840\n"
         "      y: 40\n"
         "      width: 360\n"
         "      height: 420\n"
         "    - id: Command Console\n"
         "      visible: true\n"
         "      collapsed: false\n"
         "      x: 280\n"
         "      y: 520\n"
         "      width: 640\n"
         "      height: 180\n"
         "preferences:\n"
         "  uiFontScale: 1.35\n";
}

[[nodiscard]] std::string makeToolbarHiddenConfigYaml() {
  return "version: 1\n"
         "layout:\n"
         "  windows:\n"
         "    - id: Toolbar\n"
         "      visible: false\n"
         "      collapsed: false\n"
         "      x: 24\n"
         "      y: 18\n"
         "      width: 260\n"
         "      height: 60\n";
}

void testDefaultLayoutShowsToolbarAndCorePanels() {
  if (!setupMinimalImGui()) {
    std::cout
        << "[SKIP] lxe_editor toolbar layout test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;

  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});

  ImGuiWindow *toolbar = ImGui::FindWindowByName("Toolbar");
  ImGuiWindow *sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow *inspector = ImGui::FindWindowByName("Inspector");
  ImGuiWindow *console = ImGui::FindWindowByName("Command Console");
  ImGuiWindow *stats = ImGui::FindWindowByName("Stats");

  EXPECT(toolbar != nullptr, "toolbar window should exist");
  EXPECT(sceneTree != nullptr, "scene tree window should exist");
  EXPECT(inspector != nullptr, "inspector window should exist");
  EXPECT(console != nullptr, "console window should exist");
  EXPECT(stats != nullptr, "stats window should exist");

  if (toolbar && sceneTree && inspector && console) {
    EXPECT(toolbar->Pos.y <= sceneTree->Pos.y,
           "toolbar should appear above the scene tree");
    EXPECT(sceneTree->Pos.x < inspector->Pos.x,
           "scene tree should remain left of inspector");
    EXPECT(console->Pos.y > sceneTree->Pos.y,
           "console should remain lower than the scene tree");
  }

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testDefaultLayoutDoesNotCreateViewportWindowAndUsesFullSceneRect() {
  if (!setupMinimalImGui()) {
    std::cout
        << "[SKIP] lxe_editor viewport layout test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;

  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});

  ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");
  EXPECT(viewport == nullptr,
         "viewport window should not exist in the maintained editor path");

  const auto rect = harness.ui.sceneViewRect(LX_core::Vec2f{1280.0f, 720.0f});
  EXPECT(rect.isValid(),
         "scene view rect should be valid without a viewport window");
  EXPECT(rect.x == 0.0f && rect.y == 0.0f,
         "scene view rect should start at the full-window origin");
  EXPECT(rect.width == 1280.0f && rect.height == 720.0f,
         "scene view rect should cover the full window");

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testToolbarRendersIconOnlyWithoutStaticModeText() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] toolbar icon-only test\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});
  ImGuiWindow *toolbar = ImGui::FindWindowByName("Toolbar");
  EXPECT(toolbar != nullptr, "toolbar should exist");
  if (toolbar) {
    EXPECT(
        toolbar->ContentSize.x < 250.0f,
        "toolbar content should remain compact once static labels are removed");
  }
  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testCameraRigResyncKeepsUpdatedEditorCameraPose() {
  UiHarness harness;
  auto editorCamera =
      harness.editorCameraNode->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (!editorCamera.has_value()) {
    return;
  }

  harness.rig.attach(editorCamera->get());
  harness.rig.setMode(LX_demo::lxe_editor::CameraRig::Mode::Orbit);

  harness.editorCameraNode->setTranslation({12.0f, 6.0f, 18.0f});
  editorCamera->get().updateMatrices();
  harness.rig.resyncFromAttachedCamera();

  LX_core::MockInputState input;
  harness.rig.update(input, 1.0f / 60.0f);

  EXPECT((harness.editorCameraNode->getTranslation() ==
          LX_core::Vec3f{12.0f, 6.0f, 18.0f}),
         "camera rig resync should keep the externally updated editor camera "
         "pose");
}

void testPersistedEditorConfigOverridesDefaultRectsAndPreferences() {
  namespace fs = std::filesystem;

  if (!setupMinimalImGui()) {
    std::cout
        << "[SKIP] lxe_editor persisted config test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_config";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << makePersistedEditorConfigYaml();
  UiHarness harness;
  harness.config = state.load();
  harness.ui.attach(harness.rig, harness.bus, harness.editorState,
                    harness.config, harness.viewportOverlay,
                    harness.sceneTreePanel, harness.inspectorPanel,
                    harness.consolePanel);

  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});

  ImGuiWindow *toolbar = ImGui::FindWindowByName("Toolbar");
  ImGuiWindow *sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow *console = ImGui::FindWindowByName("Command Console");

  EXPECT(toolbar != nullptr, "toolbar should exist with persisted config");
  EXPECT(sceneTree != nullptr, "scene tree should exist with persisted config");
  EXPECT(console != nullptr, "console should exist with persisted config");

  if (toolbar) {
    EXPECT(toolbar->Pos.x == 24.0f && toolbar->Pos.y == 18.0f,
           "toolbar should restore persisted position");
  }
  if (sceneTree) {
    EXPECT(sceneTree->Pos.x == 96.0f && sceneTree->Pos.y == 84.0f,
           "scene tree should restore persisted position");
    EXPECT(sceneTree->Collapsed,
           "scene tree collapsed state should restore from editor config");
  }
  if (console) {
    EXPECT(console->Pos.x == 280.0f && console->Pos.y == 520.0f,
           "console should restore persisted position");
  }
  EXPECT(ImGui::GetStyle().FontScaleMain > 1.3f,
         "persisted uiFontScale should be applied to ImGui style");

  ImGui::EndFrame();
  ImGui::DestroyContext();
  fs::remove_all(tempRoot);
}

void testEditorConfigRoundTrips() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_config_roundtrip";
  fs::remove_all(tempRoot);

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  LX_demo::lxe_editor::EditorConfigDocument document;
  document.windowPlacement = LX_core::WindowPlacement{
      .x = 42,
      .y = 64,
      .width = 1440,
      .height = 900,
      .maximized = true,
  };
  document.preferences.uiFontScale = 1.4f;
  document.layoutWindows.push_back(LX_demo::lxe_editor::EditorWindowLayout{
      .id = "Toolbar",
      .visible = true,
      .collapsed = false,
      .x = 24,
      .y = 18,
      .width = 260,
      .height = 60,
  });
  document.layoutWindows.push_back(LX_demo::lxe_editor::EditorWindowLayout{
      .id = "Scene Tree",
      .visible = false,
      .collapsed = true,
      .x = 96,
      .y = 84,
      .width = 312,
      .height = 288,
  });

  EXPECT(state.save(document), "editor config save should succeed");
  EXPECT(fs::exists(state.configPath()),
         "editor config save should create editor_config.yaml");

  const auto loaded = state.load();
  EXPECT(loaded.windowPlacement.has_value(),
         "editor config load should recover window placement");
  if (loaded.windowPlacement.has_value()) {
    EXPECT(loaded.windowPlacement->x == 42, "window x should round-trip");
    EXPECT(loaded.windowPlacement->y == 64, "window y should round-trip");
    EXPECT(loaded.windowPlacement->width == 1440,
           "window width should round-trip");
    EXPECT(loaded.windowPlacement->height == 900,
           "window height should round-trip");
    EXPECT(loaded.windowPlacement->maximized,
           "window maximized flag should round-trip");
  }
  EXPECT(loaded.layoutWindows.size() == 2, "layout windows should round-trip");
  EXPECT(loaded.preferences.uiFontScale > 1.39f &&
             loaded.preferences.uiFontScale < 1.41f,
         "preferences uiFontScale should round-trip");

  fs::remove_all(tempRoot);
}

void testPreviewModeSuppressesHotkeyDeselectAndRemove() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] lxe_editor preview hotkey suppression test (font "
                 "atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  LX_core::MockInputState input;
  harness.editorState.select({harness.targetNode});
  harness.editorState.setPreviewEnabled(true);
  harness.ui.setEditorMode(
      LX_demo::lxe_editor::UiOverlay::EditorMode::Selection);

  input.setKeyDown(LX_core::KeyCode::Escape, true);
  harness.ui.handleHotkeys(input);
  EXPECT(harness.editorState.getSelected().size() == 1,
         "preview mode should suppress deselect hotkey");
  EXPECT(harness.bus.history().empty(),
         "preview mode should not dispatch deselect commands");

  input.setKeyDown(LX_core::KeyCode::Escape, false);
  harness.ui.handleHotkeys(input);
  input.setKeyDown(LX_core::KeyCode::Delete, true);
  harness.ui.handleHotkeys(input);
  EXPECT(harness.editorState.getSelected().size() == 1,
         "preview mode should suppress remove hotkey");
  EXPECT(harness.scene->findByPath(harness.targetNode->getPath()) != nullptr,
         "preview mode should keep the selected node in the scene");
  EXPECT(harness.bus.history().empty(),
         "preview mode should not dispatch remove commands");

  ImGui::DestroyContext();
}

void testToolbarIsRecoverableFromPersistedHiddenState() {
  namespace fs = std::filesystem;

  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] lxe_editor toolbar persistence test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_toolbar_hidden";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << makeToolbarHiddenConfigYaml();

  UiHarness harness;
  harness.config = state.load();
  harness.ui.attach(harness.rig, harness.bus, harness.editorState,
                    harness.config, harness.viewportOverlay,
                    harness.sceneTreePanel, harness.inspectorPanel,
                    harness.consolePanel);

  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});
  ImGuiWindow *toolbar = ImGui::FindWindowByName("Toolbar");

  EXPECT(toolbar != nullptr,
         "toolbar should still be reachable even if persisted config hid it");
  const auto persistedToolbar =
      LX_demo::lxe_editor::findEditorWindowLayout(harness.config, "Toolbar");
  EXPECT(persistedToolbar.has_value() && persistedToolbar->get().visible,
         "toolbar visibility should recover to visible when loading a hidden "
         "toolbar state");

  ImGui::EndFrame();
  ImGui::DestroyContext();
  fs::remove_all(tempRoot);
}

void testUiFontScaleDoesNotCompoundAcrossReattach() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] lxe_editor ui font scale reattach test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  harness.config.preferences.uiFontScale = 1.35f;

  harness.ui.attach(harness.rig, harness.bus, harness.editorState,
                    harness.config, harness.viewportOverlay,
                    harness.sceneTreePanel, harness.inspectorPanel,
                    harness.consolePanel);
  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});
  const float firstFontScale = ImGui::GetStyle().FontScaleMain;
  const ImVec2 firstWindowPadding = ImGui::GetStyle().WindowPadding;
  ImGui::EndFrame();

  harness.ui.attach(harness.rig, harness.bus, harness.editorState,
                    harness.config, harness.viewportOverlay,
                    harness.sceneTreePanel, harness.inspectorPanel,
                    harness.consolePanel);
  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});
  const float secondFontScale = ImGui::GetStyle().FontScaleMain;
  const ImVec2 secondWindowPadding = ImGui::GetStyle().WindowPadding;

  EXPECT(std::abs(secondFontScale - firstFontScale) < 0.001f,
         "reattach should not compound the ui font scale");
  EXPECT(std::abs(secondWindowPadding.x - firstWindowPadding.x) < 0.001f &&
             std::abs(secondWindowPadding.y - firstWindowPadding.y) < 0.001f,
         "reattach should not compound scaled window metrics");

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testInvalidConfigFallsBackToDefaults() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_config_invalid";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: nope\npreferences: [broken";

  const auto loaded = state.load();
  EXPECT(!loaded.windowPlacement.has_value(),
         "invalid editor config should fall back to default window placement");
  EXPECT(loaded.layoutWindows.empty(),
         "invalid editor config should fall back to empty layout windows");
  EXPECT(loaded.preferences.uiFontScale == 1.0f,
         "invalid editor config should fall back to default font scale");

  fs::remove_all(tempRoot);
}

void testEditorDataRoundTripsHistory() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_data_roundtrip";
  fs::remove_all(tempRoot);

  LX_demo::lxe_editor::EditorDataState state(tempRoot);
  LX_demo::lxe_editor::EditorDataDocument document;
  document.consoleHistory = {"help", "scene list", "scene load foo.scene.yaml"};

  EXPECT(state.save(document), "editor data save should succeed");
  EXPECT(fs::exists(state.dataPath()),
         "editor data save should create editor_data.yaml");

  const auto loaded = state.load();
  EXPECT(loaded.consoleHistory.size() == 3,
         "editor data load should restore saved console history");
  EXPECT(loaded.consoleHistory[1] == "scene list",
         "editor data should preserve console history ordering");

  fs::remove_all(tempRoot);
}

void testEditorDataLoadClampsConsoleHistoryToFiftyEntries() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_data_clamp";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorDataState state(tempRoot);
  std::ofstream file(state.dataPath());
  file << "version: 1\nconsoleHistory:\n";
  for (int i = 0; i < 60; ++i) {
    file << "  - cmd-" << i << "\n";
  }
  file.close();

  const auto loaded = state.load();
  EXPECT(loaded.consoleHistory.size() == 50,
         "editor data load should keep at most 50 console history entries");
  EXPECT(!loaded.consoleHistory.empty() &&
             loaded.consoleHistory.front() == "cmd-10",
         "editor data load should drop the oldest history entries first");
  EXPECT(!loaded.consoleHistory.empty() &&
             loaded.consoleHistory.back() == "cmd-59",
         "editor data load should preserve the newest history entries");

  fs::remove_all(tempRoot);
}

void testOffscreenNativeWindowPlacementIsSanitized() {
  const LX_core::WindowPlacement placement{
      .x = 5000,
      .y = 4000,
      .width = 2600,
      .height = 1400,
      .maximized = false,
  };
  const LX_core::WindowUsableBounds bounds{
      .x = 0,
      .y = 0,
      .width = 1920,
      .height = 1080,
  };

  const auto sanitized = LX_core::sanitizeWindowPlacement(placement, bounds);
  EXPECT(sanitized.has_value(),
         "valid saved placement should sanitize against current usable bounds");
  if (sanitized.has_value()) {
    EXPECT(sanitized->x >= bounds.x &&
               sanitized->x + sanitized->width <= bounds.x + bounds.width,
           "sanitized x/width should fit within usable bounds");
    EXPECT(sanitized->y >= bounds.y &&
               sanitized->y + sanitized->height <= bounds.y + bounds.height,
           "sanitized y/height should fit within usable bounds");
    EXPECT(sanitized->width == bounds.width,
           "oversized saved width should clamp to current usable bounds");
    EXPECT(sanitized->height == bounds.height,
           "oversized saved height should clamp to current usable bounds");
  }
}

void testRestoreUsesPlacementTargetBounds() {
  const LX_core::WindowPlacement savedPlacement{
      .x = 2500,
      .y = 120,
      .width = 900,
      .height = 700,
      .maximized = false,
  };

  StubWindow targetWindow(LX_core::WindowPlacement{
      .x = 100,
      .y = 100,
      .width = 1280,
      .height = 720,
      .maximized = false,
  });
  targetWindow.setUsableBounds(LX_core::WindowUsableBounds{
      .x = 0, .y = 0, .width = 1920, .height = 1080});
  targetWindow.setUsableBoundsForPlacement(LX_core::WindowUsableBounds{
      .x = 1920, .y = 0, .width = 1920, .height = 1080});

  const auto sanitized = LX_core::sanitizeWindowPlacement(
      savedPlacement, targetWindow.getUsableBoundsForPlacement(savedPlacement));
  EXPECT(sanitized.has_value(),
         "placement should sanitize against placement-target bounds");
  if (sanitized.has_value()) {
    targetWindow.applyPlacement(*sanitized);
    EXPECT(targetWindow.appliedPlacement()->x >= 1920,
           "sanitized placement should remain on the target monitor");
  }
}

void testWindowPlacementCenterUsesWideMath() {
  const LX_core::WindowPlacement placement{
      .x = std::numeric_limits<int>::max() - 10,
      .y = std::numeric_limits<int>::min() + 10,
      .width = std::numeric_limits<int>::max(),
      .height = std::numeric_limits<int>::max(),
      .maximized = false,
  };

  const long long expectedCenterX =
      static_cast<long long>(std::numeric_limits<int>::max() - 10) +
      static_cast<long long>(std::numeric_limits<int>::max()) / 2LL;
  const long long expectedCenterY =
      static_cast<long long>(std::numeric_limits<int>::min() + 10) +
      static_cast<long long>(std::numeric_limits<int>::max()) / 2LL;

  EXPECT(
      LX_core::windowPlacementCenterX(placement) == expectedCenterX,
      "window placement center x should use wide math for large coordinates");
  EXPECT(
      LX_core::windowPlacementCenterY(placement) == expectedCenterY,
      "window placement center y should use wide math for large coordinates");
}

} // namespace

int main() {
  expSetEnvVK();
  testDefaultLayoutShowsToolbarAndCorePanels();
  testDefaultLayoutDoesNotCreateViewportWindowAndUsesFullSceneRect();
  testToolbarRendersIconOnlyWithoutStaticModeText();
  testCameraRigResyncKeepsUpdatedEditorCameraPose();
  testPersistedEditorConfigOverridesDefaultRectsAndPreferences();
  testEditorConfigRoundTrips();
  testPreviewModeSuppressesHotkeyDeselectAndRemove();
  testToolbarIsRecoverableFromPersistedHiddenState();
  testUiFontScaleDoesNotCompoundAcrossReattach();
  testInvalidConfigFallsBackToDefaults();
  testEditorDataRoundTripsHistory();
  testEditorDataLoadClampsConsoleHistoryToFiftyEntries();
  testOffscreenNativeWindowPlacementIsSanitized();
  testRestoreUsesPlacementTargetBounds();
  testWindowPlacementCenterUsesWideMath();

  if (failures == 0) {
    std::cout << "[PASS] lxe_editor layout tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
