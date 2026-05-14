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
  void updateSize(bool* closed, int* width, int* height) override {
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
  void getRequiredExtensions(std::vector<const char*>&) const override {}
  LX_core::WindowGraphicsHandle createGraphicsHandle(
      GraphicsAPI, LX_core::GraphicsInstanceHandle) const override {
    return nullptr;
  }
  void destroyGraphicsHandle(GraphicsAPI,
                             LX_core::GraphicsInstanceHandle,
                             LX_core::WindowGraphicsHandle) const override {}
  LX_core::InputStateSharedPtr getInputState() const override {
    static auto dummy = std::make_shared<LX_core::DummyInputState>();
    return dummy;
  }
  LX_core::WindowPlacement getPlacement() const override { return m_placement; }
  LX_core::WindowUsableBounds getUsableBounds() const override {
    return m_usableBounds;
  }
  void applyPlacement(const LX_core::WindowPlacement& placement) override {
    m_placement = placement;
  }
  void* getNativeHandle() const override { return nullptr; }
  void onClose(std::function<bool()>) override {}
  bool shouldClose() override { return false; }

private:
  LX_core::WindowPlacement m_placement{.x = 0, .y = 0, .width = 800, .height = 600};
  LX_core::WindowUsableBounds m_usableBounds{
      .x = 0, .y = 0, .width = 1920, .height = 1080};
};

class FakeRenderer final : public LX_core::gpu::Renderer {
public:
  void initialize(LX_core::WindowSharedPtr, const char*) override {}
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

void testSceneLoadPreservesEditorCommandHistoryAndConsole() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized, "runtime asset root should initialize for editor session");
  if (!initialized) {
    return;
  }

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  auto* const commandBusBefore = &session.commandBus();
  auto* const consoleBefore = &session.consolePanel();
  const size_t historySizeBefore = session.commandBus().history().size();

  const auto helpResult = session.commandBus().dispatch("help");
  EXPECT(helpResult.ok, "help command should succeed");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 1,
         "help command should append to editor command history");
  EXPECT(session.consolePanel().displayedText().find(helpResult.message) !=
             std::string::npos,
         "console should render command output from command bus history");

  const auto loadResult =
      session.commandBus().dispatch("scene load lxe_editor.scene.yaml");
  EXPECT(loadResult.ok, "scene load should queue a valid asset scene");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 2,
         "scene load should also contribute to command history");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneLoad(loop);

  EXPECT(&session.commandBus() == commandBusBefore,
         "scene load should preserve the editor-level command bus");
  EXPECT(&session.consolePanel() == consoleBefore,
         "scene load should preserve the editor-level console panel");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 2,
         "scene load should not clear command history");
  EXPECT(session.consolePanel().displayedText().find(helpResult.message) !=
             std::string::npos,
         "scene load should preserve earlier console output text");
  EXPECT(session.consolePanel().displayedText().find(loadResult.message) !=
             std::string::npos,
         "scene load should preserve its own console output text");
  EXPECT(renderer->initSceneCalls == 1,
         "flushing the pending scene load should restart the engine scene once");
}

void testSceneLoadFailureKeepsEditorRunningAndCurrentScene() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized, "runtime asset root should initialize for load failure test");
  if (!initialized) {
    return;
  }

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(session.scene());
  const auto oldScene = session.scene();

  const auto loadResult =
      session.commandBus().dispatch("scene load lxe_editor.scene.yaml");
  EXPECT(loadResult.ok, "valid scene load should queue before renderer failure");

  renderer->failNextInit = true;
  bool threw = false;
  try {
    session.flushPendingSceneLoad(loop);
  } catch (const std::exception& error) {
    threw = true;
    std::cerr << "[FAIL] unexpected scene load exception: " << error.what()
              << "\n";
  }

  EXPECT(!threw, "renderer scene-load failure should not escape flush");
  EXPECT(session.scene() == oldScene,
         "failed deferred scene load should keep the previous session scene");
  EXPECT(!session.currentDocumentPath().has_value(),
         "failed deferred scene load should not commit the new document path");
  EXPECT(renderer->initSceneCalls == 3,
         "failed load should try pending scene and restore the previous scene");
  EXPECT(renderer->lastScene == oldScene,
         "engine loop should be restored to the previous scene after failure");
  EXPECT(session.consolePanel().displayedText().find(
             "scene load failed: renderer rejected scene") != std::string::npos,
         "load failure should be reported in the console");
}

void testEditorHelpersUseFacetedOctahedronGeometry() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized, "runtime asset root should initialize for helper geometry test");
  if (!initialized) {
    return;
  }

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  LX_core::Scene* scene = session.scene().get();
  EXPECT(scene != nullptr, "editor session should expose a scene");
  if (!scene) {
    return;
  }

  auto requireHelperVertexCount = [&](const char* path) {
    LX_core::SceneNode* node = scene->findByPath(path);
    EXPECT(node != nullptr, std::string("helper node should exist: ") + path);
    if (!node) {
      return;
    }
    const auto mesh = node->getComponent<LX_core::MeshComponent>();
    EXPECT(mesh.has_value(), std::string("helper node should carry mesh: ") + path);
    if (!mesh.has_value()) {
      return;
    }
    EXPECT(mesh->get().getMesh() != nullptr,
           std::string("helper mesh should be present: ") + path);
    if (!mesh->get().getMesh()) {
      return;
    }
    EXPECT(mesh->get().getMesh()->vertexBuffer->getVertexCount() == 24,
           std::string("helper should use faceted octahedron vertices: ") + path);
  };

  requireHelperVertexCount("/game_cam");
  EXPECT(scene->findByPath("/game_cam/helper_camera") == nullptr,
         "camera helpers should be visual components, not child nodes");
  EXPECT(scene->findByPath("/dir_light/helper_light") == nullptr,
         "directional lights should draw through their owning node instead of a helper child");
}

void testRecordingCommandControlsSessionRecorder() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized, "runtime asset root should initialize for recording command test");
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

  const std::filesystem::path scenePath =
      resolveRuntimePath("data/scenes/session_sidecar_test.scene.yaml");
  const std::filesystem::path statePath =
      LX_demo::lxe_editor::editorSceneStatePathForScenePath(scenePath);
  std::filesystem::remove(scenePath);
  std::filesystem::remove(statePath);

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  auto* lightNode = session.scene()->findByPath("/dir_light");
  EXPECT(lightNode != nullptr, "default scene should have a light node");
  if (!lightNode) {
    return;
  }

  editorState.select({lightNode->shared_from_this()});
  rig.setOrbitTarget({1.25f, 2.5f, -3.75f});

  const auto save = session.commandBus().dispatch(
      "scene save " + scenePath.string());
  EXPECT(save.ok, "scene save should succeed");
  EXPECT(std::filesystem::exists(statePath),
         "scene save should write a same-stem editor sidecar");

  editorState.deselect();
  rig.setOrbitTarget({0.0f, 0.0f, 0.0f});

  const auto load = session.commandBus().dispatch(
      "scene load " + scenePath.string());
  EXPECT(load.ok, "scene load should queue saved local scene");

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

  std::filesystem::remove(scenePath);
  std::filesystem::remove(statePath);
}

} // namespace

int main() {
  testSceneLoadPreservesEditorCommandHistoryAndConsole();
  testSceneLoadFailureKeepsEditorRunningAndCurrentScene();
  testEditorHelpersUseFacetedOctahedronGeometry();
  testRecordingCommandControlsSessionRecorder();
  testSceneSaveLoadRoundTripsEditorSidecarState();

  if (failures != 0) {
    std::cerr << failures << " lxe_editor session test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor session tests\n";
  return 0;
}
