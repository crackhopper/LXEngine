#include "core/asset/mesh.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/builtin_commands.hpp"
#include "editor/panels/console_panel.hpp"
#include "editor/app/editor_state.hpp"
#include "editor/panels/inspector_panel.hpp"
#include "editor/panels/scene_tree_panel.hpp"
#include "editor/panels/viewport_overlay.hpp"
#include "core/input/key_code.hpp"
#include "core/input/mock_input_state.hpp"
#include "core/platform/window.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "editor/runtime/camera_rig.hpp"
#include "editor/app/editor_config_state.hpp"
#include "editor/app/editor_data_state.hpp"
#include "editor/ui/ui_overlay.hpp"

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

LX_core::MeshSharedPtr makeCenteredSquareMesh() {
  auto vb = LX_core::VertexBuffer<LX_core::VertexPos>::create(
      std::vector<LX_core::VertexPos>{
          {{-0.5f, -0.5f, 0.0f}},
          {{0.5f, -0.5f, 0.0f}},
          {{0.0f, 0.5f, 0.0f}},
      });
  auto ib = LX_core::IndexBuffer::create({0, 1, 2});
  return LX_core::Mesh::create(
      vb, ib, LX_core::BoundingBox{{-0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}});
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
    editorCameraNode->setVisibilityLayerMask(LX_core::Layer_EditorOverlay);
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

[[nodiscard]] LX_core::DisplayInfo
makeTestDisplay(const int index, std::string name, const int x, const int y,
                const int width, const int height, const float scale) {
  LX_core::DisplayInfo display;
  display.index = index;
  display.backend = "test";
  display.name = std::move(name);
  display.bounds = {.x = x, .y = y, .width = width, .height = height};
  display.usableBounds = {.x = x, .y = y, .width = width, .height = height};
  display.contentScale = scale;
  LX_core::finalizeDisplayInfo(display);
  return display;
}

[[nodiscard]] int countOccurrences(std::string_view text,
                                   std::string_view needle) {
  int count = 0;
  std::size_t offset = 0;
  while ((offset = text.find(needle, offset)) != std::string_view::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
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

void testSceneViewRectUsesRequestedWindowSizeAfterResize() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] lxe_editor scene rect resize test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;

  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});

  const auto resizedRect =
      harness.ui.sceneViewRect(LX_core::Vec2f{1920.0f, 1080.0f});
  EXPECT(resizedRect.isValid(),
         "scene view rect should remain valid after a resize query");
  EXPECT(resizedRect.x == 0.0f && resizedRect.y == 0.0f,
         "resized scene view rect should start at full-window origin");
  EXPECT(resizedRect.width == 1920.0f && resizedRect.height == 1080.0f,
         "scene view rect should derive from the requested window size");

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

void testOrbitTargetPickUsesSceneHitWithoutSelectingMarker() {
  UiHarness harness;
  auto editorCamera =
      harness.editorCameraNode->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (!editorCamera.has_value()) {
    return;
  }

  harness.targetNode->addComponent<LX_core::MeshComponent>(
      makeCenteredSquareMesh());
  editorCamera->get().setAspect(800.0f / 600.0f);
  editorCamera->get().lookAt({0.0f, 0.0f, 3.0f}, {0.0f, 1.0f, 0.0f},
                             {0.0f, 1.0f, 0.0f});
  harness.rig.attach(editorCamera->get());
  EXPECT(harness.rig.orbitTarget().y > 0.5f,
         "test setup should seed a non-origin orbit target");

  editorCamera->get().lookAt({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f},
                             {0.0f, 1.0f, 0.0f});
  LX_core::MockInputState input;
  input.setMouseButtonDown(LX_core::MouseButton::Right, true);
  input.setKeyDown(LX_core::KeyCode::M, true);
  input.setMousePosition({399.5f, 299.5f});
  harness.rig.handleOrbitTargetControls(
      input, *harness.scene,
      LX_demo::lxe_editor::SceneViewRect{
          .x = 0.0f, .y = 0.0f, .width = 800.0f, .height = 600.0f},
      1.0f / 60.0f);

  const LX_core::Vec3f target = harness.rig.orbitTarget();
  EXPECT(std::abs(target.x) < 1e-3f && std::abs(target.y) < 1e-3f &&
             std::abs(target.z) < 1e-3f,
         "right+M should move orbit target to the picked scene hit");

  const auto renderableCount = harness.scene->getRenderables().size();
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::beginFrame();
  harness.rig.enqueueDebugDraw();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() > 0,
         "orbit target marker should be drawn as debug lines");
  EXPECT(harness.scene->getRenderables().size() == renderableCount,
         "orbit target marker should not add a selectable scene node");
  LX_core::DebugDraw::endFrame();
}

void testOrbitTargetKeyboardPanMovesReferencePoint() {
  UiHarness harness;
  auto editorCamera =
      harness.editorCameraNode->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (!editorCamera.has_value()) {
    return;
  }

  editorCamera->get().lookAt({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f},
                             {0.0f, 1.0f, 0.0f});
  harness.rig.attach(editorCamera->get());
  const LX_core::Vec3f before = harness.rig.orbitTarget();

  LX_core::MockInputState input;
  input.setMouseButtonDown(LX_core::MouseButton::Right, true);
  input.setKeyDown(LX_core::KeyCode::W, true);
  harness.rig.handleOrbitTargetControls(
      input, *harness.scene,
      LX_demo::lxe_editor::SceneViewRect{
          .x = 0.0f, .y = 0.0f, .width = 800.0f, .height = 600.0f},
      1.0f);

  const LX_core::Vec3f after = harness.rig.orbitTarget();
  EXPECT(after.y > before.y && std::abs(after.x - before.x) < 1e-4f,
         "right+W should pan the orbit target upward in the camera plane");
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

void testEditorConfigV2CreatesProfilesForAllDisplays() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_display_profiles_create";
  fs::remove_all(tempRoot);

  const std::vector<LX_core::DisplayInfo> displays{
      makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f),
      makeTestDisplay(1, "Side", 1920, 0, 2560, 1440, 1.25f),
  };

  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  const auto created = state.loadOrCreateForDisplays(displays);

  EXPECT(created.version == 2, "display config should use v2 document version");
  EXPECT(created.activeDisplay == displays.front().key,
         "new display config should select the first current display");
  EXPECT(created.displayProfiles.size() == 2,
         "new display config should create one profile per display");
  EXPECT(created.displayProfiles[0].key == displays[0].key &&
             created.displayProfiles[0].available,
         "first display profile should be available");
  EXPECT(created.displayProfiles[1].key == displays[1].key &&
             created.displayProfiles[1].available,
         "second display profile should be available");
  EXPECT(fs::exists(state.configPath()),
         "load-or-create should persist editor_config.yaml on first startup");

  const auto reloaded = state.loadOrCreateForDisplays(displays);
  EXPECT(reloaded.displayProfiles.size() == 2,
         "reloaded display config should keep both display profiles");

  fs::remove_all(tempRoot);
}

void testDisplayOverrideComposesWithDefault() {
  namespace fs = std::filesystem;

  const fs::path tempRoot = fs::temp_directory_path() /
                            "lxengine_lxe_editor_display_override_compose";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << display.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  layout:\n"
                                       "    windows:\n"
                                       "      - id: Inspector\n"
                                       "        visible: true\n"
                                       "        collapsed: false\n"
                                       "        x: 840\n"
                                       "        y: 40\n"
                                       "        width: 360\n"
                                       "        height: 420\n"
                                       "  preferences:\n"
                                       "    uiFontScale: 1.0\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: \""
                                    << display.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      layout:\n"
                                       "        windows:\n"
                                       "          - id: Inspector\n"
                                       "            width: 512\n"
                                       "      preferences:\n"
                                       "        uiFontScale: 1.25\n";

  const auto document = state.loadOrCreateForDisplays({display});
  const auto effective = state.composeEffectiveConfig(document, display.key);
  const auto inspector =
      LX_demo::lxe_editor::findEditorWindowLayout(effective, "Inspector");

  EXPECT(effective.preferences.uiFontScale > 1.24f &&
             effective.preferences.uiFontScale < 1.26f,
         "display override should replace default ui font scale");
  EXPECT(inspector.has_value(), "inspector layout should compose");
  if (inspector.has_value()) {
    EXPECT(inspector->get().x == 840,
           "inspector x should remain from display default");
    EXPECT(inspector->get().width == 512,
           "inspector width should come from display override");
  }

  fs::remove_all(tempRoot);
}

void testEditorConfigV2RetainsUnavailableProfiles() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_display_profiles_retain";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto current = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  const auto missing = makeTestDisplay(1, "Side", 1920, 0, 2560, 1440, 1.25f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << missing.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  preferences:\n"
                                       "    uiFontScale: 1.0\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << current.key
                                    << "\"\n"
                                       "    label: \""
                                    << current.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides: {}\n"
                                       "  - key: \""
                                    << missing.key
                                    << "\"\n"
                                       "    label: \""
                                    << missing.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      preferences:\n"
                                       "        uiFontScale: 1.2\n";

  const auto document = state.loadOrCreateForDisplays({current});

  EXPECT(
      document.activeDisplay == current.key,
      "unavailable active display should fall back to first current display");
  EXPECT(document.displayProfiles.size() == 2,
         "sync should retain unavailable display profiles");
  const auto unavailable = std::find_if(
      document.displayProfiles.begin(), document.displayProfiles.end(),
      [&missing](const LX_demo::lxe_editor::EditorDisplayProfile &profile) {
        return profile.key == missing.key;
      });
  EXPECT(unavailable != document.displayProfiles.end(),
         "missing display profile should remain in document");
  if (unavailable != document.displayProfiles.end()) {
    EXPECT(!unavailable->available,
           "missing display profile should be marked unavailable");
  }

  fs::remove_all(tempRoot);
}

void testDisplayOverrideComposesDefaultLikeWindowAndPreferenceValues() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_display_zero_values";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << display.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  window:\n"
                                       "    x: 50\n"
                                       "    y: 60\n"
                                       "    width: 1280\n"
                                       "    height: 720\n"
                                       "    maximized: true\n"
                                       "  preferences:\n"
                                       "    uiFontScale: 1.25\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: \""
                                    << display.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      window:\n"
                                       "        x: 0\n"
                                       "        y: 0\n"
                                       "        maximized: false\n"
                                       "      preferences:\n"
                                       "        uiFontScale: 1.0\n";

  const auto document = state.loadOrCreateForDisplays({display});
  const auto effective = state.composeEffectiveConfig(document, display.key);

  EXPECT(effective.windowPlacement.has_value(),
         "window placement should compose");
  if (effective.windowPlacement.has_value()) {
    EXPECT(effective.windowPlacement->x == 0,
           "window x override should allow zero");
    EXPECT(effective.windowPlacement->y == 0,
           "window y override should allow zero");
    EXPECT(!effective.windowPlacement->maximized,
           "window maximized override should allow false");
  }
  EXPECT(effective.preferences.uiFontScale > 0.99f &&
             effective.preferences.uiFontScale < 1.01f,
         "ui font scale override should allow 1.0");

  fs::remove_all(tempRoot);
}

void testLayoutOverrideComposesDefaultLikeValues() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_layout_zero_values";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << display.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  layout:\n"
                                       "    windows:\n"
                                       "      - id: Inspector\n"
                                       "        visible: true\n"
                                       "        collapsed: true\n"
                                       "        x: 25\n"
                                       "        y: 25\n"
                                       "        width: 300\n"
                                       "        height: 200\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: \""
                                    << display.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      layout:\n"
                                       "        windows:\n"
                                       "          - id: Inspector\n"
                                       "            visible: false\n"
                                       "            collapsed: false\n"
                                       "            x: 0\n"
                                       "            y: 0\n";

  const auto document = state.loadOrCreateForDisplays({display});
  const auto effective = state.composeEffectiveConfig(document, display.key);
  const auto inspector =
      LX_demo::lxe_editor::findEditorWindowLayout(effective, "Inspector");

  EXPECT(inspector.has_value(), "inspector layout should compose");
  if (inspector.has_value()) {
    EXPECT(!inspector->get().visible,
           "layout visible override should allow false");
    EXPECT(!inspector->get().collapsed,
           "layout collapsed override should allow false");
    EXPECT(inspector->get().x == 0, "layout x override should allow zero");
    EXPECT(inspector->get().y == 0, "layout y override should allow zero");
  }

  fs::remove_all(tempRoot);
}

void testDisplaySaveDiffPersistsDefaultLikeOverrideValues() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_display_zero_save";
  fs::remove_all(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  LX_demo::lxe_editor::EditorDisplayConfigDocument document;
  document.activeDisplay = display.key;
  document.displayDefault.windowPlacement = LX_core::WindowPlacement{
      .x = 50, .y = 60, .width = 1280, .height = 720, .maximized = true};
  document.displayDefault.preferences.uiFontScale = 1.25f;
  document.displayDefault.layoutWindows.push_back(
      LX_demo::lxe_editor::EditorWindowLayout{
          .id = "Inspector",
          .visible = true,
          .collapsed = true,
          .x = 25,
          .y = 25,
          .width = 300,
          .height = 200,
      });
  document.displayProfiles.push_back(LX_demo::lxe_editor::EditorDisplayProfile{
      .key = display.key,
      .label = display.label,
      .available = true,
      .overrides = LX_demo::lxe_editor::EditorConfigOverrideDocument{},
  });

  LX_demo::lxe_editor::EditorConfigDocument effective = document.displayDefault;
  effective.windowPlacement = LX_core::WindowPlacement{
      .x = 0, .y = 0, .width = 1280, .height = 720, .maximized = false};
  effective.preferences.uiFontScale = 1.0f;
  auto inspector =
      LX_demo::lxe_editor::findEditorWindowLayout(effective, "Inspector");
  EXPECT(inspector.has_value(), "test setup should include inspector layout");
  if (inspector.has_value()) {
    inspector->get().visible = false;
    inspector->get().collapsed = false;
    inspector->get().x = 0;
    inspector->get().y = 0;
  }

  EXPECT(state.saveDisplayDocument(document, display.key, effective),
         "display document save should succeed");
  std::ifstream savedFile(state.configPath());
  std::stringstream savedBuffer;
  savedBuffer << savedFile.rdbuf();
  const std::string savedYaml = savedBuffer.str();
  EXPECT(savedYaml.find("x: 0") != std::string::npos,
         "saved override YAML should include x: 0");
  EXPECT(savedYaml.find("y: 0") != std::string::npos,
         "saved override YAML should include y: 0");
  EXPECT(savedYaml.find("maximized: false") != std::string::npos,
         "saved override YAML should include maximized: false");
  EXPECT(savedYaml.find("uiFontScale: 1") != std::string::npos,
         "saved override YAML should include uiFontScale: 1.0");
  EXPECT(savedYaml.find("visible: false") != std::string::npos,
         "saved override YAML should include visible: false");
  EXPECT(savedYaml.find("collapsed: false") != std::string::npos,
         "saved override YAML should include collapsed: false");

  const auto reloaded = state.loadOrCreateForDisplays({display});
  const auto composed = state.composeEffectiveConfig(reloaded, display.key);
  const auto composedInspector =
      LX_demo::lxe_editor::findEditorWindowLayout(composed, "Inspector");
  EXPECT(composed.windowPlacement.has_value(),
         "reloaded window placement should compose");
  if (composed.windowPlacement.has_value()) {
    EXPECT(composed.windowPlacement->x == 0,
           "reloaded x override should preserve zero");
    EXPECT(composed.windowPlacement->y == 0,
           "reloaded y override should preserve zero");
    EXPECT(!composed.windowPlacement->maximized,
           "reloaded maximized override should preserve false");
  }
  EXPECT(composed.preferences.uiFontScale > 0.99f &&
             composed.preferences.uiFontScale < 1.01f,
         "reloaded ui font scale override should preserve 1.0");
  EXPECT(composedInspector.has_value(),
         "reloaded inspector layout should compose");
  if (composedInspector.has_value()) {
    EXPECT(!composedInspector->get().visible,
           "reloaded visible override should preserve false");
    EXPECT(!composedInspector->get().collapsed,
           "reloaded collapsed override should preserve false");
    EXPECT(composedInspector->get().x == 0,
           "reloaded layout x override should preserve zero");
    EXPECT(composedInspector->get().y == 0,
           "reloaded layout y override should preserve zero");
  }

  fs::remove_all(tempRoot);
}

void testEmptyOverrideMapsDoNotMutateEffectiveConfig() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_empty_overrides";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << display.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  window:\n"
                                       "    x: 50\n"
                                       "    y: 60\n"
                                       "    width: 1280\n"
                                       "    height: 720\n"
                                       "    maximized: true\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: \""
                                    << display.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      window: {}\n";

  const auto document = state.loadOrCreateForDisplays({display});
  const auto effective = state.composeEffectiveConfig(document, display.key);

  EXPECT(effective.windowPlacement.has_value(),
         "default window placement should remain present");
  if (effective.windowPlacement.has_value()) {
    EXPECT(effective.windowPlacement->x == 50,
           "empty window override should not replace default x");
    EXPECT(effective.windowPlacement->y == 60,
           "empty window override should not replace default y");
    EXPECT(effective.windowPlacement->width == 1280,
           "empty window override should not replace default width");
    EXPECT(effective.windowPlacement->height == 720,
           "empty window override should not replace default height");
    EXPECT(effective.windowPlacement->maximized,
           "empty window override should not replace default maximized");
  }

  fs::remove_all(tempRoot);
}

void testIdOnlyLayoutOverrideEntriesAreIgnored() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_id_only_overrides";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << display.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  layout:\n"
                                       "    windows:\n"
                                       "      - id: Inspector\n"
                                       "        visible: true\n"
                                       "        collapsed: false\n"
                                       "        x: 25\n"
                                       "        y: 25\n"
                                       "        width: 300\n"
                                       "        height: 200\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: \""
                                    << display.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      layout:\n"
                                       "        windows:\n"
                                       "          - id: Inspector\n"
                                       "          - id: Floating\n";

  const auto document = state.loadOrCreateForDisplays({display});
  const auto effective = state.composeEffectiveConfig(document, display.key);
  const auto inspector =
      LX_demo::lxe_editor::findEditorWindowLayout(effective, "Inspector");
  const auto floating =
      LX_demo::lxe_editor::findEditorWindowLayout(effective, "Floating");

  EXPECT(inspector.has_value(), "default inspector layout should remain");
  if (inspector.has_value()) {
    EXPECT(inspector->get().x == 25 && inspector->get().y == 25,
           "id-only override should not mutate default layout position");
  }
  EXPECT(!floating.has_value(),
         "id-only override should not create a new zero-size layout");

  fs::remove_all(tempRoot);
}

void testLoadOrCreateMigratesV1ToDurableV2() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_migrate_v1_durable";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << makePersistedEditorConfigYaml();

  (void)state.loadOrCreateForDisplays({display});
  const auto reloaded = state.loadOrCreateForDisplays({display});
  std::ifstream savedFile(state.configPath());
  std::stringstream savedBuffer;
  savedBuffer << savedFile.rdbuf();
  const std::string savedYaml = savedBuffer.str();

  EXPECT(savedYaml.find("version: 2") != std::string::npos,
         "v1 migration should be persisted as v2");
  EXPECT(savedYaml.find("displayDefault:") != std::string::npos,
         "migrated v2 file should contain displayDefault");
  EXPECT(reloaded.displayProfiles.size() == 1,
         "migrated v2 reload should keep current display profile");
  EXPECT(reloaded.displayDefault.windowPlacement.has_value(),
         "migrated v2 reload should preserve v1 window placement");

  fs::remove_all(tempRoot);
}

void testLoadOrCreatePersistsDisplaySyncAndActiveFallback() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_sync_v2_durable";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto current = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  const auto added = makeTestDisplay(1, "Side", 1920, 0, 2560, 1440, 1.25f);
  const auto missing = makeTestDisplay(2, "Gone", 4480, 0, 1280, 720, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << missing.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  preferences:\n"
                                       "    uiFontScale: 1.0\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << current.key
                                    << "\"\n"
                                       "    label: \""
                                    << current.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides: {}\n"
                                       "  - key: \""
                                    << missing.key
                                    << "\"\n"
                                       "    label: \""
                                    << missing.label
                                    << "\"\n"
                                       "    available: true\n"
                                       "    overrides: {}\n";

  (void)state.loadOrCreateForDisplays({current, added});
  std::ifstream savedFile(state.configPath());
  std::stringstream savedBuffer;
  savedBuffer << savedFile.rdbuf();
  const std::string savedYaml = savedBuffer.str();
  const auto reloaded = state.loadOrCreateForDisplays({current, added});

  EXPECT(savedYaml.find("activeDisplay: " + current.key) != std::string::npos ||
             savedYaml.find("activeDisplay: \"" + current.key + "\"") !=
                 std::string::npos,
         "active display fallback should be written to disk");
  EXPECT(savedYaml.find(added.key) != std::string::npos,
         "new display profile should be written to disk");
  EXPECT(savedYaml.find("available: false") != std::string::npos,
         "unavailable display profile should be written to disk");
  EXPECT(reloaded.activeDisplay == current.key,
         "active display fallback should be durable");
  EXPECT(reloaded.displayProfiles.size() == 3,
         "display sync should persist retained and new profiles");
  const auto addedProfile = std::find_if(
      reloaded.displayProfiles.begin(), reloaded.displayProfiles.end(),
      [&added](const LX_demo::lxe_editor::EditorDisplayProfile &profile) {
        return profile.key == added.key;
      });
  const auto missingProfile = std::find_if(
      reloaded.displayProfiles.begin(), reloaded.displayProfiles.end(),
      [&missing](const LX_demo::lxe_editor::EditorDisplayProfile &profile) {
        return profile.key == missing.key;
      });
  EXPECT(addedProfile != reloaded.displayProfiles.end() &&
             addedProfile->available,
         "new display profile should be persisted as available");
  EXPECT(missingProfile != reloaded.displayProfiles.end() &&
             !missingProfile->available,
         "missing display profile should be persisted as unavailable");

  fs::remove_all(tempRoot);
}

void testDuplicateDisplayProfilesNormalizeOnLoadCreate() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_duplicate_profiles";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  std::ofstream(state.configPath()) << "version: 2\n"
                                       "activeDisplay: \""
                                    << display.key
                                    << "\"\n"
                                       "displayDefault:\n"
                                       "  preferences:\n"
                                       "    uiFontScale: 1.0\n"
                                       "displayProfiles:\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: first profile\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      preferences:\n"
                                       "        uiFontScale: 1.25\n"
                                       "  - key: \""
                                    << display.key
                                    << "\"\n"
                                       "    label: duplicate profile\n"
                                       "    available: true\n"
                                       "    overrides:\n"
                                       "      preferences:\n"
                                       "        uiFontScale: 1.5\n";

  (void)state.loadOrCreateForDisplays({display});
  const auto reloaded = state.loadOrCreateForDisplays({display});
  std::ifstream savedFile(state.configPath());
  std::stringstream savedBuffer;
  savedBuffer << savedFile.rdbuf();
  const std::string savedYaml = savedBuffer.str();

  EXPECT(reloaded.displayProfiles.size() == 1,
         "duplicate profile keys should normalize to one profile");
  EXPECT(reloaded.displayProfiles.front().label == display.label,
         "current display sync should update the retained profile label");
  const auto effective = state.composeEffectiveConfig(reloaded, display.key);
  EXPECT(effective.preferences.uiFontScale > 1.24f &&
             effective.preferences.uiFontScale < 1.26f,
         "duplicate normalization should preserve the first profile overrides");
  EXPECT(countOccurrences(savedYaml, display.key) == 2,
         "saved YAML should contain the display key once in activeDisplay and "
         "once in displayProfiles");

  fs::remove_all(tempRoot);
}

void testLoadOrCreateDoesNotOverwriteInvalidYaml() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_invalid_v2_no_overwrite";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  const std::string invalidYaml = "version: 2\npreferences: [broken";
  std::ofstream(state.configPath()) << invalidYaml;

  const auto document = state.loadOrCreateForDisplays({display});
  std::ifstream savedFile(state.configPath());
  std::stringstream savedBuffer;
  savedBuffer << savedFile.rdbuf();

  EXPECT(document.version == 2,
         "invalid existing config should still return in-memory v2 defaults");
  EXPECT(savedBuffer.str() == invalidYaml,
         "invalid existing config should not be overwritten");

  fs::remove_all(tempRoot);
}

void testLoadOrCreateDoesNotOverwriteUnsupportedVersion() {
  namespace fs = std::filesystem;

  const fs::path tempRoot = fs::temp_directory_path() /
                            "lxengine_lxe_editor_unsupported_no_overwrite";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  const auto display = makeTestDisplay(0, "Primary", 0, 0, 1920, 1080, 1.0f);
  LX_demo::lxe_editor::EditorConfigState state(tempRoot);
  const std::string unsupportedYaml = "version: 999\n"
                                      "activeDisplay: future-display\n";
  std::ofstream(state.configPath()) << unsupportedYaml;

  const auto document = state.loadOrCreateForDisplays({display});
  std::ifstream savedFile(state.configPath());
  std::stringstream savedBuffer;
  savedBuffer << savedFile.rdbuf();

  EXPECT(
      document.version == 2,
      "unsupported existing config should still return in-memory v2 defaults");
  EXPECT(savedBuffer.str().find("version: 999") != std::string::npos,
         "unsupported existing config version should remain on disk");
  EXPECT(savedBuffer.str() == unsupportedYaml,
         "unsupported existing config should not be overwritten");

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

void testDisplayNextHotkeyDispatchesCommandOnce() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] lxe_editor display hotkey test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  LX_core::MockInputState input;
  input.setKeyDown(LX_core::KeyCode::LCtrl, true);
  input.setKeyDown(LX_core::KeyCode::LShift, true);
  input.setKeyDown(LX_core::KeyCode::D, true);

  harness.ui.handleHotkeys(input);
  harness.ui.handleHotkeys(input);

  EXPECT(harness.bus.history().size() == 1,
         "display next hotkey should dispatch once while held");
  if (!harness.bus.history().empty()) {
    EXPECT(harness.bus.history().front().line == "display next",
           "display next hotkey should dispatch the shared display command");
  }

  input.setKeyDown(LX_core::KeyCode::D, false);
  harness.ui.handleHotkeys(input);
  input.setKeyDown(LX_core::KeyCode::D, true);
  harness.ui.handleHotkeys(input);

  EXPECT(harness.bus.history().size() == 2,
         "display next hotkey should dispatch again after key release");

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

void testBuiltinAssetsVisibilityRestoresFromPersistedLayout() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] lxe_editor builtin assets visibility test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  harness.config.layoutWindows.push_back(
      LX_demo::lxe_editor::EditorWindowLayout{
          .id = "Builtin Assets",
          .visible = false,
          .collapsed = false,
          .x = 304,
          .y = 84,
          .width = 360,
          .height = 420,
      });
  harness.ui.attach(harness.rig, harness.bus, harness.editorState,
                    harness.config, harness.viewportOverlay,
                    harness.sceneTreePanel, harness.inspectorPanel,
                    harness.consolePanel);

  ImGui::NewFrame();
  harness.ui.drawFrame({1280.0f, 720.0f});

  ImGuiWindow *builtinAssets = ImGui::FindWindowByName("Builtin Assets");
  const auto persistedBuiltinAssets =
      LX_demo::lxe_editor::findEditorWindowLayout(harness.config,
                                                  "Builtin Assets");
  EXPECT(builtinAssets == nullptr,
         "hidden builtin assets layout should keep the panel closed");
  EXPECT(persistedBuiltinAssets.has_value() &&
             !persistedBuiltinAssets->get().visible,
         "builtin assets visibility should remain hidden in editor config");

  ImGui::EndFrame();
  ImGui::DestroyContext();
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
  document.lastProject = fs::path("data") / "projects" / "demo";
  document.consoleHistory = {
      "help", "project init empty demo", "scene open main"};

  EXPECT(state.save(document), "editor data save should succeed");
  EXPECT(fs::exists(state.dataPath()),
         "editor data save should create editor_data.yaml");
  std::ifstream savedFile(state.dataPath());
  std::stringstream savedContents;
  savedContents << savedFile.rdbuf();
  EXPECT(savedContents.str().find("lastProject: data/projects/demo") !=
             std::string::npos,
         "editor data save should write the last project path");

  const auto loaded = state.load();
  EXPECT(loaded.lastProject.has_value(),
         "editor data load should restore the saved last project");
  EXPECT(loaded.lastProject == fs::path("data/projects/demo"),
         "editor data load should preserve the last project path");
  EXPECT(loaded.consoleHistory.size() == 3,
         "editor data load should restore saved console history");
  EXPECT(loaded.consoleHistory[1] == "project init empty demo",
         "editor data should preserve console history ordering");

  LX_demo::lxe_editor::EditorDataDocument documentWithoutLastProject;
  documentWithoutLastProject.consoleHistory = {"help"};
  EXPECT(state.save(documentWithoutLastProject),
         "editor data save without last project should succeed");
  const auto loadedWithoutLastProject = state.load();
  EXPECT(!loadedWithoutLastProject.lastProject.has_value(),
         "editor data load should not invent a last project when absent");
  EXPECT(loadedWithoutLastProject.consoleHistory.size() == 1 &&
             loadedWithoutLastProject.consoleHistory.front() == "help",
         "editor data should still round-trip history without last project");

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
  file << "version: 1\nlastProject: data/projects/demo\nconsoleHistory:\n";
  for (int i = 0; i < 60; ++i) {
    file << "  - cmd-" << i << "\n";
  }
  file.close();

  const auto loaded = state.load();
  EXPECT(loaded.lastProject.has_value(),
         "editor data load should read an existing last project path");
  EXPECT(loaded.lastProject == fs::path("data/projects/demo"),
         "editor data load should preserve an existing last project path");
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

void testEditorDataIgnoresNonScalarLastProject() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_data_bad_project";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorDataState state(tempRoot);
  std::ofstream file(state.dataPath());
  file << "version: 1\n"
          "lastProject:\n"
          "  nested: value\n"
          "consoleHistory:\n"
          "  - help\n";
  file.close();

  const auto loaded = state.load();
  EXPECT(!loaded.lastProject.has_value(),
         "editor data load should ignore non-scalar last project values");
  EXPECT(loaded.consoleHistory.size() == 1 &&
             loaded.consoleHistory.front() == "help",
         "editor data load should still read history after bad last project");

  fs::remove_all(tempRoot);
}

void testEditorDataUnsupportedVersionFallsBackToDefaults() {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      fs::temp_directory_path() / "lxengine_lxe_editor_editor_data_bad_version";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  LX_demo::lxe_editor::EditorDataState state(tempRoot);
  std::ofstream file(state.dataPath());
  file << "version: 2\n"
          "lastProject: data/projects/demo\n"
          "consoleHistory:\n"
          "  - help\n";
  file.close();

  const auto loaded = state.load();
  EXPECT(loaded.version == 1,
         "unsupported editor data version should fall back to default version");
  EXPECT(!loaded.lastProject.has_value(),
         "unsupported editor data version should not load last project");
  EXPECT(loaded.consoleHistory.empty(),
         "unsupported editor data version should not load history");

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
  testSceneViewRectUsesRequestedWindowSizeAfterResize();
  testToolbarRendersIconOnlyWithoutStaticModeText();
  testCameraRigResyncKeepsUpdatedEditorCameraPose();
  testOrbitTargetPickUsesSceneHitWithoutSelectingMarker();
  testOrbitTargetKeyboardPanMovesReferencePoint();
  testPersistedEditorConfigOverridesDefaultRectsAndPreferences();
  testEditorConfigRoundTrips();
  testEditorConfigV2CreatesProfilesForAllDisplays();
  testDisplayOverrideComposesWithDefault();
  testEditorConfigV2RetainsUnavailableProfiles();
  testDisplayOverrideComposesDefaultLikeWindowAndPreferenceValues();
  testLayoutOverrideComposesDefaultLikeValues();
  testDisplaySaveDiffPersistsDefaultLikeOverrideValues();
  testEmptyOverrideMapsDoNotMutateEffectiveConfig();
  testIdOnlyLayoutOverrideEntriesAreIgnored();
  testLoadOrCreateMigratesV1ToDurableV2();
  testLoadOrCreatePersistsDisplaySyncAndActiveFallback();
  testDuplicateDisplayProfilesNormalizeOnLoadCreate();
  testLoadOrCreateDoesNotOverwriteInvalidYaml();
  testLoadOrCreateDoesNotOverwriteUnsupportedVersion();
  testPreviewModeSuppressesHotkeyDeselectAndRemove();
  testDisplayNextHotkeyDispatchesCommandOnce();
  testToolbarIsRecoverableFromPersistedHiddenState();
  testBuiltinAssetsVisibilityRestoresFromPersistedLayout();
  testUiFontScaleDoesNotCompoundAcrossReattach();
  testInvalidConfigFallsBackToDefaults();
  testEditorDataRoundTripsHistory();
  testEditorDataLoadClampsConsoleHistoryToFiftyEntries();
  testEditorDataIgnoresNonScalarLastProject();
  testEditorDataUnsupportedVersionFallsBackToDefaults();
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
