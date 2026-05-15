#include "demos/lxe_editor/editor_session.hpp"

#include "core/editor/console_panel.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/input/dummy_input_state.hpp"
#include "core/rhi/renderer.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/lxe_editor/editor_scene_state.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

class FakeWindow final : public LX_core::Window {
public:
  int getWidth() const override { return 800; }
  int getHeight() const override { return 600; }
  void updateSize(bool *closed, int *width, int *height) override {
    if (closed) {
      *closed = false;
    }
    if (width) {
      *width = getWidth();
    }
    if (height) {
      *height = getHeight();
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
    static auto dummy = std::make_shared<LX_core::DummyInputState>();
    return dummy;
  }
  LX_core::WindowPlacement getPlacement() const override { return m_placement; }
  LX_core::WindowUsableBounds getUsableBounds() const override {
    return m_usableBounds;
  }
  void applyPlacement(const LX_core::WindowPlacement &placement) override {
    m_placement = placement;
  }
  void *getNativeHandle() const override { return nullptr; }
  void onClose(std::function<bool()>) override {}
  bool shouldClose() override { return false; }

private:
  LX_core::WindowPlacement m_placement{
      .x = 0, .y = 0, .width = 800, .height = 600};
  LX_core::WindowUsableBounds m_usableBounds{
      .x = 0, .y = 0, .width = 1920, .height = 1080};
};

class FakeRenderer final : public LX_core::gpu::Renderer {
public:
  void initialize(LX_core::WindowSharedPtr, const char *) override {}
  void shutdown() override {}
  void initScene(LX_core::SceneSharedPtr scene) override {
    ++initSceneCalls;
    lastScene = std::move(scene);
    if (failNextInit) {
      failNextInit = false;
      throw std::runtime_error("renderer rejected scene");
    }
  }
  void uploadData() override {}
  void draw() override {}

  int initSceneCalls = 0;
  bool failNextInit = false;
  LX_core::SceneSharedPtr lastScene;
};

void cleanupProject(const std::string &projectName) {
  std::filesystem::remove_all(resolveRuntimePath("data/projects") /
                              projectName);
}

void testProjectSceneOpenPreservesEditorCommandHistoryAndConsole() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for editor session");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_history");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  auto *const commandBusBefore = &session.commandBus();
  auto *const consoleBefore = &session.consolePanel();
  const size_t historySizeBefore = session.commandBus().history().size();

  const auto helpResult = session.commandBus().dispatch("help");
  EXPECT(helpResult.ok, "help command should succeed");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 1,
         "help command should append to editor command history");
  EXPECT(session.consolePanel().displayedText().find(helpResult.message) !=
             std::string::npos,
         "console should render command output from command bus history");

  const auto initResult = session.commandBus().dispatch(
      "project init empty editor_session_history");
  EXPECT(initResult.ok, "project init should open a new project");
  EXPECT(session.currentProjectId() ==
             std::optional<std::string>("editor_session_history"),
         "project init should expose the current project id");
  EXPECT(session.activeScenePath().has_value(),
         "project init should expose the active scene path");

  const auto saveResult = session.commandBus().dispatch("scene save");
  EXPECT(saveResult.ok, "scene save should write the active project scene");

  const auto openResult = session.commandBus().dispatch("scene open main");
  EXPECT(openResult.ok, "scene open should queue a project scene");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 4,
         "project and scene commands should contribute to command history");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneLoad(loop);

  EXPECT(&session.commandBus() == commandBusBefore,
         "scene open should preserve the editor-level command bus");
  EXPECT(&session.consolePanel() == consoleBefore,
         "scene open should preserve the editor-level console panel");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 4,
         "scene open should not clear command history");
  EXPECT(session.consolePanel().displayedText().find(helpResult.message) !=
             std::string::npos,
         "scene open should preserve earlier console output text");
  EXPECT(session.consolePanel().displayedText().find(openResult.message) !=
             std::string::npos,
         "scene open should preserve its own console output text");
  EXPECT(
      renderer->initSceneCalls == 1,
      "flushing the pending scene open should restart the engine scene once");

  cleanupProject("editor_session_history");
}

void testSceneOpenFailureKeepsEditorRunningAndCurrentScene() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for load failure test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_failure");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_failure")
             .ok,
         "project init should succeed before load failure test");
  EXPECT(session.commandBus().dispatch("scene save").ok,
         "scene save should make the project main scene loadable");
  EXPECT(session.commandBus().dispatch("scene duplicate main alternate").ok,
         "scene duplicate should create a second project scene");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(session.scene());
  const auto oldScene = session.scene();

  renderer->failNextInit = true;
  bool threw = false;
  try {
    session.flushPendingSceneLoad(loop);
  } catch (const std::exception &error) {
    threw = true;
    std::cerr << "[FAIL] unexpected scene open exception: " << error.what()
              << "\n";
  }

  EXPECT(!threw, "renderer scene-open failure should not escape flush");
  EXPECT(session.scene() == oldScene,
         "failed deferred scene open should keep the previous session scene");
  EXPECT(
      session.currentDocumentPath() == session.activeScenePath(),
      "failed deferred scene open should expose the project active scene path");
  EXPECT(renderer->initSceneCalls == 3,
         "failed load should try pending scene and restore the previous scene");
  EXPECT(renderer->lastScene == oldScene,
         "engine loop should be restored to the previous scene after failure");
  EXPECT(session.consolePanel().displayedText().find(
             "scene load failed: renderer rejected scene") != std::string::npos,
         "load failure should be reported in the console");

  cleanupProject("editor_session_failure");
}

void testEditorDoesNotCreateCameraOrLightHelperNodes() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for helper geometry test");
  if (!initialized) {
    return;
  }

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  LX_core::Scene *scene = session.scene().get();
  EXPECT(scene != nullptr, "editor session should expose a scene");
  if (!scene) {
    return;
  }

  LX_core::SceneNode *gameCamera = scene->findByPath("/game_cam");
  EXPECT(gameCamera != nullptr, "game camera node should exist");
  if (gameCamera) {
    EXPECT(!gameCamera->getComponent<LX_core::MeshComponent>().has_value(),
           "game camera should not carry a solid helper mesh");
    EXPECT(scene->getPickBounds(*gameCamera).isValid(),
           "game camera should remain selectable through debug pick bounds");
  }
  EXPECT(scene->findByPath("/game_cam/helper_camera") == nullptr,
         "camera helpers should not exist as child nodes");
  EXPECT(scene->findByPath("/dir_light/helper_light") == nullptr,
         "directional lights should not create helper child nodes");
}

void testRecordingCommandControlsSessionRecorder() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for recording command test");
  if (!initialized) {
    return;
  }

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  const auto initial = session.commandBus().dispatch("recording status");
  EXPECT(initial.ok, "recording status command should succeed");
  EXPECT(initial.structured.find("\"enabled\":false") != std::string::npos,
         "recording should default disabled");

  EXPECT(session.commandBus().dispatch("recording enable").ok,
         "recording enable command should succeed");
  EXPECT(session.commandBus().dispatch("recording start basic").ok,
         "recording start command should succeed");
  EXPECT(session.recording().status().active,
         "recording start command should activate recorder");

  const auto stop = session.commandBus().dispatch("recording stop discard");
  EXPECT(stop.ok, "recording stop command should succeed");
  EXPECT(!session.recording().status().active,
         "recording stop command should deactivate recorder");
}

void testSceneSaveLoadRoundTripsEditorSidecarState() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized, "runtime asset root should initialize for sidecar test");
  if (!initialized) {
    return;
  }

  cleanupProject("editor_session_sidecar");
  const std::filesystem::path scenePath = resolveRuntimePath(
      "data/projects/editor_session_sidecar/scenes/main.scene.yaml");
  const std::filesystem::path statePath =
      LX_demo::lxe_editor::editorSceneStatePathForScenePath(scenePath);
  std::filesystem::remove(statePath);

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();
  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_sidecar")
             .ok,
         "project init should succeed for sidecar test");

  auto *lightNode = session.scene()->findByPath("/dir_light");
  EXPECT(lightNode != nullptr, "default scene should have a light node");
  if (!lightNode) {
    return;
  }

  editorState.select({lightNode->shared_from_this()});
  rig.setOrbitTarget({1.25f, 2.5f, -3.75f});

  const auto save = session.commandBus().dispatch("scene save");
  EXPECT(save.ok, "scene save should succeed");
  EXPECT(std::filesystem::exists(statePath),
         "scene save should write a same-stem editor sidecar");

  editorState.deselect();
  rig.setOrbitTarget({0.0f, 0.0f, 0.0f});

  const auto load = session.commandBus().dispatch("scene open main");
  EXPECT(load.ok, "scene open should queue saved project scene");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneLoad(loop);

  const LX_core::Vec3f target = rig.orbitTarget();
  EXPECT(std::abs(target.x - 1.25f) < 0.001f &&
             std::abs(target.y - 2.5f) < 0.001f &&
             std::abs(target.z + 3.75f) < 0.001f,
         "sidecar should restore orbit target");
  const auto selected = editorState.getSelected();
  EXPECT(selected.size() == 1 && selected.front()->getPath() == "/dir_light",
         "sidecar should restore selected scene node paths");

  std::filesystem::remove(statePath);
  cleanupProject("editor_session_sidecar");
}

} // namespace

int main() {
  testProjectSceneOpenPreservesEditorCommandHistoryAndConsole();
  testSceneOpenFailureKeepsEditorRunningAndCurrentScene();
  testEditorDoesNotCreateCameraOrLightHelperNodes();
  testRecordingCommandControlsSessionRecorder();
  testSceneSaveLoadRoundTripsEditorSidecarState();

  if (failures != 0) {
    std::cerr << failures << " lxe_editor session test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor session tests\n";
  return 0;
}
