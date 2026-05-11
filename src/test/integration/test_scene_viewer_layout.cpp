#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/platform/window.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "demos/scene_viewer/camera_rig.hpp"
#include "demos/scene_viewer/ui_overlay.hpp"
#include "demos/scene_viewer/window_layout_state.hpp"

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
  LX_demo::scene_viewer::CameraRig rig;
  LX_core::SceneTreePanel sceneTreePanel;
  LX_core::InspectorPanel inspectorPanel;
  LX_core::ConsolePanel consolePanel;
  LX_core::ViewportOverlay viewportOverlay;
  LX_demo::scene_viewer::UiOverlay ui;

  UiHarness()
      : scene(LX_core::Scene::create("layout_scene")),
        editorCameraNode(LX_core::SceneNode::create("editor_camera")),
        sceneTreePanel(bus, editorState, *scene),
        inspectorPanel(bus, editorState),
        consolePanel(bus),
        viewportOverlay(bus, editorState, *scene) {
    editorCameraNode->setName("editor_cam");
    auto editorCamera =
        editorCameraNode->addComponent<LX_core::CameraComponent>();
    scene->addCamera(editorCameraNode);
    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(editorCameraNode);
    (void)editorState.syncActiveCamera(*scene);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
    rig.attach(editorCamera->get());
    ui.attach(rig, bus, sceneTreePanel, inspectorPanel, consolePanel,
              viewportOverlay);
  }
};

[[nodiscard]] std::string makePersistedLayoutIni() {
  return
      "[Window][Scene Tree]\n"
      "Pos=96,84\n"
      "Size=312,288\n"
      "Collapsed=1\n"
      "\n"
      "[Window][Inspector]\n"
      "Pos=840,40\n"
      "Size=360,420\n"
      "Collapsed=0\n"
      "\n"
      "[Window][Command Console]\n"
      "Pos=280,520\n"
      "Size=640,180\n"
      "Collapsed=0\n"
      "\n"
      "[Window][Viewport]\n"
      "Pos=280,24\n"
      "Size=640,460\n"
      "Collapsed=0\n";
}

class StubWindow final : public LX_core::Window {
public:
  explicit StubWindow(const LX_core::WindowPlacement& placement)
      : m_placement(placement) {}

  int getWidth() const override { return m_placement.width; }
  int getHeight() const override { return m_placement.height; }
  void updateSize(bool* closed, int* width, int* height) override {
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
  void getRequiredExtensions(std::vector<const char*>&) const override {}
  LX_core::WindowGraphicsHandle
  createGraphicsHandle(GraphicsAPI,
                       LX_core::GraphicsInstanceHandle) const override {
    return nullptr;
  }
  void destroyGraphicsHandle(GraphicsAPI, LX_core::GraphicsInstanceHandle,
                             LX_core::WindowGraphicsHandle) const override {}
  LX_core::InputStateSharedPtr getInputState() const override { return nullptr; }
  void* getNativeHandle() const override { return nullptr; }
  void onClose(std::function<bool()>) override {}
  bool shouldClose() override { return false; }
  LX_core::WindowPlacement getPlacement() const override { return m_placement; }
  LX_core::WindowUsableBounds getUsableBounds() const override {
    return m_usableBounds;
  }
  LX_core::WindowUsableBounds
  getUsableBoundsForPlacement(
      const LX_core::WindowPlacement& placement) const override {
    (void)placement;
    return m_placementUsableBounds.value_or(m_usableBounds);
  }
  void applyPlacement(const LX_core::WindowPlacement& placement) override {
    m_placement = placement;
    m_appliedPlacement = placement;
  }

  void setUsableBounds(const LX_core::WindowUsableBounds& bounds) {
    m_usableBounds = bounds;
  }
  void setUsableBoundsForPlacement(const LX_core::WindowUsableBounds& bounds) {
    m_placementUsableBounds = bounds;
  }

  [[nodiscard]] const std::optional<LX_core::WindowPlacement>&
  appliedPlacement() const {
    return m_appliedPlacement;
  }

private:
  LX_core::WindowPlacement m_placement{};
  LX_core::WindowUsableBounds m_usableBounds{.x = 0, .y = 0, .width = 1920, .height = 1080};
  std::optional<LX_core::WindowUsableBounds> m_placementUsableBounds;
  std::optional<LX_core::WindowPlacement> m_appliedPlacement;
};

class CerrCapture final {
public:
  CerrCapture() : m_previous(std::cerr.rdbuf(m_stream.rdbuf())) {}
  ~CerrCapture() { std::cerr.rdbuf(m_previous); }

  [[nodiscard]] std::string str() const { return m_stream.str(); }

private:
  std::ostringstream m_stream;
  std::streambuf* m_previous = nullptr;
};

[[nodiscard]] usize countSubstring(const std::string& text,
                                   const std::string_view needle) {
  if (needle.empty()) {
    return 0;
  }

  usize count = 0;
  size_t offset = 0;
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
}

void testDefaultLayoutPlacesViewportBetweenPanels() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer layout test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;

  ImGui::NewFrame();
  harness.ui.drawFrame();

  ImGuiWindow *sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow *inspector = ImGui::FindWindowByName("Inspector");
  ImGuiWindow *console = ImGui::FindWindowByName("Command Console");
  ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");

  EXPECT(sceneTree != nullptr, "scene tree window should exist");
  EXPECT(inspector != nullptr, "inspector window should exist");
  EXPECT(console != nullptr, "console window should exist");
  EXPECT(viewport != nullptr, "viewport window should exist");

  if (sceneTree && inspector && console && viewport) {
    EXPECT(sceneTree->Pos.x < viewport->Pos.x,
           "scene tree should be left of viewport");
    EXPECT(inspector->Pos.x > viewport->Pos.x,
           "inspector should be right of viewport");
    EXPECT(console->Pos.y >= viewport->Pos.y + viewport->Size.y - 1.0f,
           "console should be below viewport");
    EXPECT(harness.viewportOverlay.getPanelRect().size.x > 0.0f &&
               harness.viewportOverlay.getPanelRect().size.y > 0.0f,
           "viewport overlay should bind to viewport content rect");
  }

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testDefaultLayoutReflowsAfterResize() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer resize layout test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;

  ImGuiIO &io = ImGui::GetIO();

  ImGui::NewFrame();
  harness.ui.drawFrame();
  ImGui::EndFrame();

  io.DisplaySize = ImVec2(1600.0f, 900.0f);
  ImGui::NewFrame();
  harness.ui.drawFrame();

  ImGuiWindow *sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow *inspector = ImGui::FindWindowByName("Inspector");
  ImGuiWindow *console = ImGui::FindWindowByName("Command Console");
  ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");

  EXPECT(sceneTree != nullptr, "scene tree window should exist after resize");
  EXPECT(inspector != nullptr, "inspector window should exist after resize");
  EXPECT(console != nullptr, "console window should exist after resize");
  EXPECT(viewport != nullptr, "viewport window should exist after resize");

  if (sceneTree && inspector && console && viewport) {
    EXPECT(viewport->Size.x > 900.0f,
           "viewport width should grow with display resize");
    EXPECT(viewport->Size.y > 600.0f,
           "viewport height should grow with display resize");
    EXPECT(inspector->Pos.x + inspector->Size.x <= io.DisplaySize.x + 1.0f,
           "inspector should stay within resized display bounds");
    EXPECT(console->Pos.y + console->Size.y <= io.DisplaySize.y + 1.0f,
           "console should stay within resized display bounds");
  }

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testDefaultLayoutKeepsEditorPanelsVisibleAcrossFrames() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer persistent layout test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;

  ImGui::NewFrame();
  harness.ui.drawFrame();
  ImGui::EndFrame();

  ImGui::NewFrame();
  harness.ui.drawFrame();

  const int currentFrame = ImGui::GetFrameCount();
  ImGuiWindow *sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow *inspector = ImGui::FindWindowByName("Inspector");
  ImGuiWindow *console = ImGui::FindWindowByName("Command Console");
  ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");

  EXPECT(sceneTree != nullptr && sceneTree->LastFrameActive == currentFrame,
         "scene tree window should still be drawn on frame 2");
  EXPECT(inspector != nullptr && inspector->LastFrameActive == currentFrame,
         "inspector window should still be drawn on frame 2");
  EXPECT(console != nullptr && console->LastFrameActive == currentFrame,
         "console window should still be drawn on frame 2");
  EXPECT(viewport != nullptr && viewport->LastFrameActive == currentFrame,
         "viewport window should still be drawn on frame 2");

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testPersistedLayoutOverridesDefaultWindowRects() {
  namespace fs = std::filesystem;

  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer persisted layout override test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_persisted_layout";
  const std::string persistedLayout = makePersistedLayoutIni();
  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot);
  std::ofstream(state.imguiLayoutPath()) << persistedLayout;
  const bool restored = state.restoreImGuiLayout();
  EXPECT(restored, "scene_viewer should restore usable persisted layout");
  harness.ui.setDefaultLayoutEnabled(!restored);

  ImGui::NewFrame();
  harness.ui.drawFrame();

  ImGuiWindow* sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport");

  EXPECT(sceneTree != nullptr, "scene tree window should exist with persisted layout");
  EXPECT(viewport != nullptr, "viewport window should exist with persisted layout");

  if (sceneTree) {
    EXPECT(sceneTree->Pos.x == 96.0f && sceneTree->Pos.y == 84.0f,
           "scene tree should keep persisted position instead of default snapping");
    EXPECT(sceneTree->Collapsed,
           "scene tree collapsed state should restore from persisted layout");
  }
  if (viewport) {
    EXPECT(viewport->Pos.x == 280.0f && viewport->Pos.y == 24.0f,
           "viewport should keep persisted position instead of default snapping");
    EXPECT(viewport->Size.x == 640.0f && viewport->Size.y == 460.0f,
           "viewport should keep persisted size");
  }

  ImGui::EndFrame();
  ImGui::DestroyContext();
  fs::remove_all(tempRoot);
}

void testWindowLayoutStateRoundTripsAndFailsSoft() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_layout_test";
  fs::remove_all(tempRoot);

  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);

  EXPECT(!state.restoreImGuiLayout(),
         "missing imgui layout file should fall back to defaults");
  EXPECT(!state.loadNativeWindowPlacement().has_value(),
         "missing native window layout file should fail soft");
  EXPECT(!LX_demo::scene_viewer::WindowLayoutState::parseNativeWindowPlacement(
              "version=1\nx=bad\ny=20\nwidth=1280\nheight=720\nmaximized=0\n")
              .has_value(),
         "corrupt native window layout text should fail soft");
  EXPECT(LX_demo::scene_viewer::WindowLayoutState::isUsableImGuiLayout(
              "[Window][Inspector]\nPos=1,2\nSize=3,4\nCollapsed=0\n"),
         "single-window imgui layout should be treated as usable");
  EXPECT(!LX_demo::scene_viewer::WindowLayoutState::hasSceneViewerCoreLayout(
              "[Window][Inspector]\nPos=1,2\nSize=3,4\nCollapsed=0\n"),
         "single-window imgui layout should not be authoritative for scene_viewer");
  EXPECT(!LX_demo::scene_viewer::WindowLayoutState::hasSceneViewerCoreLayout(
              "[Window][Scene Tree]\n"
              "[Window][Inspector]\n"
              "[Window][Command Console]\n"
              "[Window][Viewport]\n"),
         "core window headers without usable settings should not be authoritative");
  EXPECT(!LX_demo::scene_viewer::WindowLayoutState::hasSceneViewerCoreLayout(
              "[Window][Scene Tree]\n"
              "Collapsed=0\n"
              "[Window][Inspector]\n"
              "Collapsed=0\n"
              "[Window][Command Console]\n"
              "Collapsed=0\n"
              "[Window][Viewport]\n"
              "Collapsed=0\n"),
         "collapsed-only core sections should not disable default scene_viewer layout");
  EXPECT(!LX_demo::scene_viewer::WindowLayoutState::hasSceneViewerCoreLayout(
              "[Window][Scene Tree]\n"
              "Pos=bad\n"
              "Size=312,288\n"
              "[Window][Inspector]\n"
              "Pos=840,40\n"
              "Size=oops\n"
              "[Window][Command Console]\n"
              "Pos=280,520\n"
              "Size=640,180\n"
              "[Window][Viewport]\n"
              "Pos=280,24\n"
              "Size=640,460\n"),
         "malformed core geometry payloads should not disable default scene_viewer layout");

  const LX_core::WindowPlacement placement{
      .x = 42,
      .y = 64,
      .width = 1440,
      .height = 900,
      .maximized = true,
  };
  state.saveNativeWindowPlacement(placement);

  const auto loadedPlacement = state.loadNativeWindowPlacement();
  EXPECT(loadedPlacement.has_value(), "saved native window layout should round-trip");
  if (loadedPlacement.has_value()) {
    EXPECT(loadedPlacement->x == placement.x, "native window x should round-trip");
    EXPECT(loadedPlacement->y == placement.y, "native window y should round-trip");
    EXPECT(loadedPlacement->width == placement.width,
           "native window width should round-trip");
    EXPECT(loadedPlacement->height == placement.height,
           "native window height should round-trip");
    EXPECT(loadedPlacement->maximized == placement.maximized,
           "native window maximized should round-trip");
  }

  StubWindow sourceWindow(placement);
  state.captureNativeWindowPlacement(sourceWindow);

  StubWindow targetWindow(LX_core::WindowPlacement{
      .x = 0,
      .y = 0,
      .width = 1280,
      .height = 720,
      .maximized = false,
  });
  state.restoreNativeWindowPlacement(targetWindow);
  EXPECT(targetWindow.appliedPlacement().has_value(),
         "restoring native window layout should apply placement to window");
  if (targetWindow.appliedPlacement().has_value()) {
    EXPECT(targetWindow.appliedPlacement()->x == placement.x,
           "applied x should match saved placement");
    EXPECT(targetWindow.appliedPlacement()->y == placement.y,
           "applied y should match saved placement");
    EXPECT(targetWindow.appliedPlacement()->width == placement.width,
           "applied width should match saved placement");
    EXPECT(targetWindow.appliedPlacement()->height == placement.height,
           "applied height should match saved placement");
    EXPECT(targetWindow.appliedPlacement()->maximized == placement.maximized,
           "applied maximized should match saved placement");
  }

  {
    CerrCapture capture;
    std::ofstream(state.windowStatePath())
        << "version=1\nx=bad\ny=20\nwidth=1280\nheight=720\nmaximized=0\n";
    EXPECT(!state.loadNativeWindowPlacement().has_value(),
           "corrupt native window state file should fail soft");
    EXPECT(capture.str().find("warning") != std::string::npos &&
               capture.str().find("window_state.ini") != std::string::npos,
           "corrupt native window state should log a clear warning");
  }

  std::ofstream(state.imguiLayoutPath()) << "corrupt";
  {
    EXPECT(setupMinimalImGui(),
           "imgui context should initialize for corrupt layout warning test");
    CerrCapture capture;
    EXPECT(!state.restoreImGuiLayout(),
           "corrupt imgui layout file should fail soft");
    EXPECT(capture.str().find("warning") != std::string::npos &&
               capture.str().find("layout.ini") != std::string::npos,
           "corrupt imgui layout should log a clear warning");
    ImGui::DestroyContext();
  }

  fs::remove_all(tempRoot);
}

void testPartialSceneViewerLayoutKeepsDefaultCoreLayout() {
  namespace fs = std::filesystem;

  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer partial layout fallback test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_partial_layout";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);
  std::ofstream(state.imguiLayoutPath())
      << "[Window][Scene Tree]\n"
         "Pos=777,111\n"
         "Size=123,222\n"
         "Collapsed=0\n";

  const bool restored = state.restoreImGuiLayout();
  EXPECT(restored, "parseable partial imgui layout should still load generically");
  EXPECT(!state.hasAuthoritativeSceneViewerLayout(),
         "partial scene_viewer layout should not disable default core layout");
  harness.ui.setDefaultLayoutEnabled(!state.hasAuthoritativeSceneViewerLayout());

  ImGui::NewFrame();
  harness.ui.drawFrame();

  ImGuiWindow* sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
  ImGuiWindow* console = ImGui::FindWindowByName("Command Console");
  ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport");

  EXPECT(sceneTree != nullptr, "scene tree should exist with partial persisted layout");
  EXPECT(inspector != nullptr, "inspector should exist with partial persisted layout");
  EXPECT(console != nullptr, "console should exist with partial persisted layout");
  EXPECT(viewport != nullptr, "viewport should exist with partial persisted layout");

  if (sceneTree && inspector && console && viewport) {
    EXPECT(sceneTree->Pos.x < viewport->Pos.x,
           "scene tree should still use default left-panel placement");
    EXPECT(inspector->Pos.x > viewport->Pos.x,
           "inspector should still use default right-panel placement");
    EXPECT(console->Pos.y >= viewport->Pos.y + viewport->Size.y - 1.0f,
           "console should still use default bottom-panel placement");
    EXPECT(sceneTree->Pos.x != 777.0f || sceneTree->Pos.y != 111.0f,
           "partial persisted layout should not become authoritative for core windows");
  }

  ImGui::EndFrame();
  ImGui::DestroyContext();
  fs::remove_all(tempRoot);
}

void testImGuiLayoutSaveAndReloadRoundTrip() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_layout_round_trip";
  fs::remove_all(tempRoot);

  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer layout round-trip test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);

  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(123.0f, 67.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(456.0f, 234.0f), ImGuiCond_Always);
  bool open = true;
  ImGui::Begin("Round Trip Window", &open);
  ImGui::TextUnformatted("round trip");
  ImGui::End();
  ImGui::EndFrame();
  state.saveImGuiLayout();
  ImGui::DestroyContext();

  EXPECT(fs::exists(state.imguiLayoutPath()),
         "saving imgui layout should create the local layout file");

  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer layout reload test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    fs::remove_all(tempRoot);
    return;
  }

  EXPECT(state.restoreImGuiLayout(),
         "saved imgui layout should reload successfully");
  ImGui::NewFrame();
  ImGui::Begin("Round Trip Window", &open);
  ImGui::TextUnformatted("round trip");
  ImGuiWindow* roundTrip = ImGui::FindWindowByName("Round Trip Window");
  EXPECT(roundTrip != nullptr, "round-trip window should exist after reload");
  if (roundTrip) {
    EXPECT(roundTrip->Pos.x == 123.0f && roundTrip->Pos.y == 67.0f,
           "round-trip restored window position should match saved layout");
    EXPECT(roundTrip->Size.x == 456.0f && roundTrip->Size.y == 234.0f,
           "round-trip restored window size should match saved layout");
  }
  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext();
  fs::remove_all(tempRoot);
}

void testInvalidNativeWindowGeometryIsNotPersisted() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_invalid_window_state";
  fs::remove_all(tempRoot);

  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);
  const LX_core::WindowPlacement validPlacement{
      .x = 10,
      .y = 20,
      .width = 1280,
      .height = 720,
      .maximized = false,
  };
  state.saveNativeWindowPlacement(validPlacement);
  const auto originalText = [&]() -> std::string {
    std::ifstream in(state.windowStatePath(), std::ios::in | std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }();

  const LX_core::WindowPlacement invalidPlacement{
      .x = 30,
      .y = 40,
      .width = 0,
      .height = 720,
      .maximized = false,
  };
  state.saveNativeWindowPlacement(invalidPlacement);

  EXPECT(fs::exists(state.windowStatePath()),
         "invalid native geometry should not remove the existing window state file");
  const auto rewrittenText = [&]() -> std::string {
    std::ifstream in(state.windowStatePath(), std::ios::in | std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }();
  EXPECT(rewrittenText == originalText,
         "invalid native geometry should not overwrite the last valid window state");

  StubWindow invalidWindow(invalidPlacement);
  state.captureNativeWindowPlacement(invalidWindow);
  const auto capturedText = [&]() -> std::string {
    std::ifstream in(state.windowStatePath(), std::ios::in | std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }();
  EXPECT(capturedText == originalText,
         "capturing invalid native geometry should keep the last valid window state");

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

  const auto sanitized =
      LX_core::sanitizeWindowPlacement(placement, bounds);
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
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_secondary_monitor";
  fs::remove_all(tempRoot);

  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);
  const LX_core::WindowPlacement savedPlacement{
      .x = 2500,
      .y = 120,
      .width = 900,
      .height = 700,
      .maximized = false,
  };
  state.saveNativeWindowPlacement(savedPlacement);

  StubWindow targetWindow(LX_core::WindowPlacement{
      .x = 100,
      .y = 100,
      .width = 1280,
      .height = 720,
      .maximized = false,
  });
  targetWindow.setUsableBounds(LX_core::WindowUsableBounds{
      .x = 0,
      .y = 0,
      .width = 1920,
      .height = 1080,
  });
  targetWindow.setUsableBoundsForPlacement(LX_core::WindowUsableBounds{
      .x = 1920,
      .y = 0,
      .width = 1920,
      .height = 1080,
  });

  state.restoreNativeWindowPlacement(targetWindow);
  EXPECT(targetWindow.appliedPlacement().has_value(),
         "restore should apply placement when target-monitor bounds are available");
  if (targetWindow.appliedPlacement().has_value()) {
    EXPECT(targetWindow.appliedPlacement()->x >= 1920,
           "restore should sanitize against the target monitor selected from saved placement");
    EXPECT(targetWindow.appliedPlacement()->x == savedPlacement.x,
           "restore should preserve saved secondary-monitor x when it is valid there");
  }

  fs::remove_all(tempRoot);
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

  EXPECT(LX_core::windowPlacementCenterX(placement) == expectedCenterX,
         "window placement center x should use wide math for large coordinates");
  EXPECT(LX_core::windowPlacementCenterY(placement) == expectedCenterY,
         "window placement center y should use wide math for large coordinates");
}

void testWriteFailuresLogWarnings() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_scene_viewer_write_failure";
  fs::remove_all(tempRoot);
  std::ofstream(tempRoot) << "block writes";

  LX_demo::scene_viewer::WindowLayoutState state(tempRoot);

  {
    EXPECT(setupMinimalImGui(),
           "imgui context should initialize for save warning test");
    ImGui::NewFrame();
    bool open = true;
    ImGui::Begin("Write Failure Window", &open);
    ImGui::TextUnformatted("persist");
    ImGui::End();
    ImGui::EndFrame();

    CerrCapture capture;
    ImGui::GetIO().WantSaveIniSettings = true;
    state.maybeSaveImGuiLayout();
    state.maybeSaveImGuiLayout();
    EXPECT(countSubstring(capture.str(), "layout.ini") == 1,
           "repeated failed imgui layout writes should warn only once per pending save");
    ImGui::DestroyContext();
  }

  {
    CerrCapture capture;
    state.saveNativeWindowPlacement(LX_core::WindowPlacement{
        .x = 1,
        .y = 2,
        .width = 1280,
        .height = 720,
        .maximized = false,
    });
    EXPECT(capture.str().find("warning") != std::string::npos &&
               capture.str().find("window_state.ini") != std::string::npos,
           "native window state write failure should log a clear warning");
  }

  fs::remove_all(tempRoot);
}

} // namespace

int main() {
  expSetEnvVK();
  testDefaultLayoutPlacesViewportBetweenPanels();
  testDefaultLayoutReflowsAfterResize();
  testDefaultLayoutKeepsEditorPanelsVisibleAcrossFrames();
  testPersistedLayoutOverridesDefaultWindowRects();
  testWindowLayoutStateRoundTripsAndFailsSoft();
  testPartialSceneViewerLayoutKeepsDefaultCoreLayout();
  testImGuiLayoutSaveAndReloadRoundTrip();
  testInvalidNativeWindowGeometryIsNotPersisted();
  testOffscreenNativeWindowPlacementIsSanitized();
  testRestoreUsesPlacementTargetBounds();
  testWindowPlacementCenterUsesWideMath();
  testWriteFailuresLogWarnings();

  if (failures == 0) {
    std::cout << "[PASS] scene_viewer layout tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
