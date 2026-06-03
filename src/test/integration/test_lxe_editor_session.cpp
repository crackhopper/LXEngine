#include "demos/lxe_editor/editor_session.hpp"

#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/console_panel.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/input/dummy_input_state.hpp"
#include "core/rhi/renderer.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/lxe_editor/editor_scene_state.hpp"
#include "demos/lxe_editor/realtime_render_profile.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
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

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path &path,
                   const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << contents;
}

class ScopedFileBackup final {
public:
  explicit ScopedFileBackup(std::filesystem::path path)
      : m_path(std::move(path)) {
    if (std::filesystem::exists(m_path)) {
      m_contents = readTextFile(m_path);
    }
  }

  ~ScopedFileBackup() {
    if (m_contents.has_value()) {
      writeTextFile(m_path, *m_contents);
      return;
    }
    std::error_code ec;
    std::filesystem::remove(m_path, ec);
  }

  ScopedFileBackup(const ScopedFileBackup &) = delete;
  ScopedFileBackup &operator=(const ScopedFileBackup &) = delete;

private:
  std::filesystem::path m_path;
  std::optional<std::string> m_contents;
};

void testProjectSceneOpenPreservesEditorCommandHistoryAndConsole() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for editor session");
  if (!initialized) {
    return;
  }
  cleanupProject("empty");
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

  const auto templateInitResult =
      session.commandBus().dispatch("project init empty");
  EXPECT(templateInitResult.ok,
         "project init without explicit name should open a template-named "
         "project");
  EXPECT(session.currentProjectDisplayName() ==
             std::optional<std::string>("Empty"),
         "project init should expose the template display name separately from "
         "the project id");

  const auto closeTemplateResult =
      session.commandBus().dispatch("project close");
  EXPECT(closeTemplateResult.ok,
         "project close should clear the template-named project");

  const auto initResult = session.commandBus().dispatch(
      "project init empty editor_session_history");
  EXPECT(initResult.ok, "project init should open a new project");
  EXPECT(session.currentProjectId() ==
             std::optional<std::string>("editor_session_history"),
         "project init should expose the current project id");
  EXPECT(session.currentProjectDisplayName() ==
             std::optional<std::string>("editor_session_history"),
         "project init with an explicit name should expose the project display "
         "name");
  EXPECT(session.activeScenePath().has_value(),
         "project init should expose the active scene path");

  const auto earlySaveResult = session.commandBus().dispatch("scene save");
  EXPECT(!earlySaveResult.ok,
         "scene save should wait until the queued project scene is loaded");

  const auto openResult = session.commandBus().dispatch("scene open main");
  EXPECT(openResult.ok, "scene open should queue a project scene");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 6,
         "project and scene commands should contribute to command history");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  const auto saveResult = session.commandBus().dispatch("scene save");
  EXPECT(saveResult.ok, "scene save should write the active project scene");

  EXPECT(&session.commandBus() == commandBusBefore,
         "scene open should preserve the editor-level command bus");
  EXPECT(&session.consolePanel() == consoleBefore,
         "scene open should preserve the editor-level console panel");
  EXPECT(session.commandBus().history().size() == historySizeBefore + 7,
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

  cleanupProject("empty");
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

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  EXPECT(session.commandBus().dispatch("scene save").ok,
         "scene save should make the project main scene openable");
  EXPECT(session.commandBus().dispatch("scene duplicate main alternate").ok,
         "scene duplicate should create a second project scene");

  loop.startScene(session.scene());
  const auto oldScene = session.scene();

  renderer->failNextInit = true;
  bool threw = false;
  try {
    session.flushPendingSceneOpen(loop);
  } catch (const std::exception &error) {
    threw = true;
    std::cerr << "[FAIL] unexpected scene open exception: " << error.what()
              << "\n";
  }

  EXPECT(!threw, "renderer scene-open failure should not escape flush");
  EXPECT(session.scene() == oldScene,
         "failed deferred scene open should keep the previous session scene");
  EXPECT(
      session.runtimeScenePath() != session.activeScenePath(),
      "failed deferred scene open should leave the loaded document unchanged");
  const auto stateScene = session.commandBus().dispatch("state scene");
  EXPECT(stateScene.ok, "state scene should succeed after failed scene open");
  EXPECT(stateScene.structured.find(session.runtimeScenePath()->string()) !=
             std::string::npos,
         "state scene should report the loaded runtime document path");
  EXPECT(stateScene.structured.find(session.activeScenePath()->string()) ==
             std::string::npos,
         "state scene should not report the pending active scene path");
  const auto failedSave = session.commandBus().dispatch("scene save");
  EXPECT(!failedSave.ok,
         "scene save should not write while the active project scene is not "
         "loaded");
  EXPECT(renderer->initSceneCalls == 4,
         "failed open should try pending scene and restore the previous scene");
  EXPECT(renderer->lastScene == oldScene,
         "engine loop should be restored to the previous scene after failure");
  EXPECT(session.consolePanel().displayedText().find(
             "scene open failed: renderer rejected scene") !=
             std::string::npos,
         "open failure should be reported in the console");

  cleanupProject("editor_session_failure");
}

void testStartupClosesProjectWhenLastProjectSceneCannotLoad() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for startup fallback test");
  if (!initialized) {
    return;
  }

  cleanupProject("editor_session_broken_start");
  const std::filesystem::path editorDataPath =
      resolveRuntimePath("data/lxe_editor/editor_data.yaml");
  ScopedFileBackup editorDataBackup(editorDataPath);
  const std::filesystem::path projectRoot =
      resolveRuntimePath("data/projects/editor_session_broken_start");
  writeTextFile(projectRoot / "project.yaml",
                "schema: lxe.project.v1\n"
                "id: editor_session_broken_start\n"
                "displayName: Broken Start\n"
                "activeScene: scenes/missing.scene.yaml\n"
                "scenes:\n"
                "  - id: main\n"
                "    path: scenes/missing.scene.yaml\n");
  writeTextFile(editorDataPath,
                "version: 1\n"
                "lastProject: data/projects/editor_session_broken_start\n"
                "consoleHistory: []\n");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.currentProjectId() == std::optional<std::string>("lxe_default"),
         "startup should fall back to the default project when last project "
         "cannot load");
  EXPECT(session.currentProjectActiveScene() ==
             std::optional<std::string>("scenes/lxe_editor.scene.yaml"),
         "default project fallback should open the lxe_editor scene");
  EXPECT(session.runtimeScenePath().has_value(),
         "default project fallback should load a saved runtime scene");
  const auto status = session.commandBus().dispatch("project status");
  EXPECT(status.ok &&
             status.structured.find("\"id\":\"lxe_default\"") !=
                 std::string::npos,
         "project status should report the default project after startup "
         "fallback");
  EXPECT(readTextFile(editorDataPath).find("lxe_default") !=
             std::string::npos,
         "startup fallback should persist the default project as lastProject");

  cleanupProject("editor_session_broken_start");
  cleanupProject("lxe_default");
}

void testStartupWithoutLastProjectOpensDefaultProject() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for default startup test");
  if (!initialized) {
    return;
  }

  cleanupProject("lxe_default");
  const std::filesystem::path editorDataPath =
      resolveRuntimePath("data/lxe_editor/editor_data.yaml");
  ScopedFileBackup editorDataBackup(editorDataPath);
  std::filesystem::remove(editorDataPath);

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.currentProjectId() == std::optional<std::string>("lxe_default"),
         "startup without lastProject should open lxe_default");
  EXPECT(session.currentProjectActiveScene() ==
             std::optional<std::string>("scenes/lxe_editor.scene.yaml"),
         "startup default project should use lxe_editor as active scene");
  EXPECT(session.runtimeScenePath().has_value(),
         "startup default project should load a runtime scene");
  if (session.runtimeScenePath().has_value()) {
    EXPECT(session.runtimeScenePath()->filename() == "lxe_editor.scene.yaml",
           "startup default runtime should load lxe_editor scene");
  }
  EXPECT(session.scene()->findByPath("/game_cam") != nullptr,
         "startup default scene should contain the game camera");
  EXPECT(readTextFile(editorDataPath).find("lxe_default") !=
             std::string::npos,
         "startup default project should be persisted as lastProject");

  cleanupProject("lxe_default");
}

void testSceneOpenClearsSelectionAndDebugDrawState() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for reload cleanup test");
  if (!initialized) {
    return;
  }

  cleanupProject("lxe_default");

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

  auto *helmet = session.scene()->findByPath("/helmet");
  EXPECT(helmet != nullptr, "default lxe_editor scene should have helmet");
  if (!helmet) {
    cleanupProject("lxe_default");
    return;
  }
  editorState.select({helmet->shared_from_this()});
  EXPECT(!editorState.getSelected().empty(),
         "test should start with an active selection");

  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(session.scene());
  LX_core::DebugDraw::beginFrame();
  LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  (void)LX_core::DebugDraw::endFrame();
  EXPECT(LX_core::DebugDraw::testing::hasRenderable(
             LX_core::Layer_EditorOverlay),
         "test should start with debug draw renderable attached");

  const auto open = session.commandBus().dispatch("scene open ibl_metal_sphere");
  EXPECT(open.ok, "scene open should queue another default-project scene");
  session.flushPendingSceneOpen(loop);

  EXPECT(editorState.getSelected().empty(),
         "scene open reload should clear stale selection");
  EXPECT(session.runtimeScenePath().has_value() &&
             session.runtimeScenePath()->filename() ==
                 "ibl_metal_sphere.scene.yaml",
         "scene open reload should load the requested scene");
  EXPECT(!LX_core::DebugDraw::testing::hasRenderable(
             LX_core::Layer_EditorOverlay),
         "scene open reload should clear old debug draw renderables");

  LX_core::DebugDraw::reset();
  cleanupProject("lxe_default");
}

void testProjectCloseCancelsPendingSceneOpen() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for project close test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_close_pending");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_close_pending")
             .ok,
         "project init should queue the template scene");
  const auto closeResult = session.commandBus().dispatch("project close");
  EXPECT(closeResult.ok, "project close should succeed");
  EXPECT(!session.currentProjectId().has_value(),
         "project close should clear the current project");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  EXPECT(renderer->initSceneCalls == 1,
         "project close should replace the engine scene with an empty scene");
  EXPECT(!session.runtimeScenePath().has_value(),
         "project close should leave the editor on an unsaved empty scene");

  cleanupProject("editor_session_close_pending");
}

void testProjectCloseAfterLoadedSceneUpdatesEngineLoop() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for loaded project close test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_close_loaded");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_close_loaded")
             .ok,
         "project init should queue the project scene");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);
  EXPECT(renderer->lastScene == session.scene(),
         "project init flush should bind the project scene");
  const auto loadedScene = session.scene();

  const auto closeResult = session.commandBus().dispatch("project close");
  EXPECT(closeResult.ok, "project close should succeed after a loaded scene");
  EXPECT(session.scene() == loadedScene,
         "project close should not rebuild bindings inside command dispatch");
  session.flushPendingSceneOpen(loop);

  EXPECT(session.scene() != loadedScene,
         "project close flush should replace the editor runtime scene");
  EXPECT(renderer->lastScene == session.scene(),
         "project close flush should replace the engine scene");
  EXPECT(!session.currentProjectId().has_value(),
         "project close should clear the current project");
  EXPECT(!session.runtimeScenePath().has_value(),
         "project close should leave no loaded document path");

  cleanupProject("editor_session_close_loaded");
}

void testProjectListMessageIncludesProjectIdsAndPaths() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for project list message test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_list_alpha");
  cleanupProject("editor_session_list_beta");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_list_alpha")
             .ok,
         "first project init should succeed");
  EXPECT(session.commandBus().dispatch("project close").ok,
         "project close before second init should succeed");
  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_list_beta")
             .ok,
         "second project init should succeed");

  const auto listResult = session.commandBus().dispatch("project list");

  EXPECT(listResult.ok, "project list should succeed");
  EXPECT(listResult.message.find("editor_session_list_alpha") !=
             std::string::npos,
         "project list message should include first project id");
  EXPECT(listResult.message.find("editor_session_list_beta") !=
             std::string::npos,
         "project list message should include second project id");
  EXPECT(listResult.message.find("project.yaml") != std::string::npos,
         "project list message should include project file paths");

  cleanupProject("editor_session_list_alpha");
  cleanupProject("editor_session_list_beta");
}

void testProjectSaveMessageIncludesProjectAndScenePaths() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for project save message test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_save_message");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init empty editor_session_save_message")
             .ok,
         "project init should queue scene open");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  const auto saveResult = session.commandBus().dispatch("project save");

  EXPECT(saveResult.ok, "project save should succeed");
  EXPECT(saveResult.message.find("project.yaml") != std::string::npos,
         "project save message should include project.yaml path");
  EXPECT(saveResult.message.find("scenes/main.scene.yaml") !=
             std::string::npos,
         "project save message should include active scene path");

  cleanupProject("editor_session_save_message");
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

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

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

  session.flushPendingSceneOpen(loop);

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

void testRenderDebugDumpCommandUsesRegisteredHook() {
  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);

  std::string capturedAttachment;
  std::optional<std::string> capturedCameraPath;
  std::optional<std::filesystem::path> capturedPath;
  session.initialize(
      {},
      LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks{
          .dumpRenderTarget =
              [&](std::string_view attachment,
                  const std::optional<std::string> &cameraPath,
                  const std::filesystem::path &path) {
                capturedAttachment = std::string(attachment);
                capturedCameraPath = cameraPath;
                capturedPath = path;
                return LX_demo::lxe_editor::LxeEditorSession::
                    RenderDebugDumpResult{
                    .path = path,
                    .width = 1024,
                    .height = 1024,
                    .format = "D32_SFLOAT",
                };
              },
      });

  const auto dump = session.commandBus().dispatch(
      "render debug dump shadow.cascade0 data/debug/dump/shadow_cascade0.bmp");

  EXPECT(dump.ok, "render debug dump should succeed when hook is registered");
  EXPECT(capturedAttachment == "shadow.cascade0",
         "render debug dump should forward the attachment name");
  EXPECT(capturedPath.has_value() &&
             capturedPath->generic_string() ==
                 "data/debug/dump/shadow_cascade0.bmp",
         "render debug dump should forward the optional output path");
  EXPECT(dump.structured.find("\"path\":\"data/debug/dump/shadow_cascade0.bmp\"") !=
             std::string::npos,
         "render debug dump should return the output path in JSON");
  EXPECT(dump.structured.find("\"screenPath\"") == std::string::npos,
         "render debug dump should not add a swapchain screenshot path");

  const auto passDump = session.commandBus().dispatch(
      "render debug dump Forward /editor_cam data/debug/dump/forward.bmp");
  EXPECT(passDump.ok, "render debug dump should accept a pass camera path");
  EXPECT(capturedAttachment == "Forward",
         "render debug dump should forward the pass target");
  EXPECT(capturedCameraPath.has_value() &&
             *capturedCameraPath == "/editor_cam",
         "render debug dump should forward the optional camera path");

  const auto defaultDump =
      session.commandBus().dispatch("render debug dump shadow.cascade0");
  EXPECT(defaultDump.ok, "render debug dump should create a default path");
  EXPECT(defaultDump.structured.find("\"path\":\"data/debug/dump/") !=
             std::string::npos,
         "render debug dump should default to the short dump directory");
  EXPECT(defaultDump.structured.find("shadow_cascade0.bmp") !=
             std::string::npos,
         "render debug dump should derive the target name in the default path");

  LX_demo::lxe_editor::LxeEditorSession unavailableSession(rig, ui,
                                                           editorState);
  unavailableSession.initialize();
  const auto unavailable =
      unavailableSession.commandBus().dispatch("render debug dump shadow.cascade0");
  EXPECT(!unavailable.ok,
         "render debug dump should fail clearly when no hook is registered");
}

void testRealtimeRenderListUsesCurrentSceneOutputProfiles() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for realtime profile list test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_realtime_list");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init pbr_ibl editor_session_realtime_list")
             .ok,
         "project init should load a scene with output profiles");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  const auto list = session.commandBus().dispatch("realtime-render ls");

  EXPECT(list.ok, "realtime-render ls should succeed");
  EXPECT(list.structured.find("\"defaultOutputProfile\":\"preview\"") !=
             std::string::npos,
         "list JSON should include the current scene default output profile");
  EXPECT(list.structured.find("\"name\":\"preview\"") != std::string::npos,
         "list JSON should include preview profile");
  EXPECT(list.structured.find("\"name\":\"mvp\"") != std::string::npos,
         "list JSON should include mvp profile");
  EXPECT(list.structured.find("\"width\":1024") != std::string::npos,
         "list JSON should include resolved profile dimensions");
  EXPECT(list.structured.find("\"outDir\":\"artifacts/offline/mvp\"") !=
             std::string::npos,
         "list JSON should include profile output directory");

  cleanupProject("editor_session_realtime_list");
}

void testRealtimeRenderListUsesDefaultProfilesForSceneWithoutProfiles() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for realtime fallback test");
  if (!initialized) {
    return;
  }

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  const auto list = session.commandBus().dispatch("realtime-render ls");

  EXPECT(list.ok,
         "realtime-render ls should succeed for a scene without profiles");
  EXPECT(list.structured.find("\"defaultOutputProfile\":\"preview\"") !=
             std::string::npos,
         "fallback list JSON should include the built-in default profile");
  EXPECT(list.structured.find("\"name\":\"preview\"") != std::string::npos,
         "fallback list JSON should include preview");
  EXPECT(list.structured.find("\"width\":512") != std::string::npos,
         "fallback list JSON should include the default width");
  EXPECT(list.structured.find("\"outDir\":\"artifacts\"") !=
             std::string::npos,
         "fallback list JSON should include the default output directory");
}

void testRealtimeRenderRunCallsHookAndReturnsStructuredOutput() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for realtime profile run test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_realtime_run");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);

  bool hookCalled = false;
  LX_core::SceneSharedPtr capturedScene;
  LX_demo::lxe_editor::RealtimeProfileOutputRequest capturedRequest;
  session.initialize(
      {}, {},
      LX_demo::lxe_editor::LxeEditorSession::RealtimeRenderProfileHooks{
          .generate =
              [&](LX_core::SceneSharedPtr scene,
                  const LX_demo::lxe_editor::RealtimeProfileOutputRequest
                      &request) {
                hookCalled = true;
                capturedScene = std::move(scene);
                capturedRequest = request;
                const std::filesystem::path base = request.outputBasePath;
                return LX_demo::lxe_editor::RealtimeProfileOutputResult{
                    .linearExrPath = base.parent_path() / "linear.exr",
                    .cpuSrgbPngPath = base.parent_path() / "cpu.png",
                    .pipelineSrgbPngPath =
                        base.parent_path() / "pipeline.png",
                    .metadataPath = base.parent_path() / "render.json",
                    .width = request.output.width,
                    .height = request.output.height,
                };
              },
      });

  EXPECT(session.commandBus()
             .dispatch("project init pbr_ibl editor_session_realtime_run")
             .ok,
         "project init should load a scene with output profiles");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  const auto run = session.commandBus().dispatch("realtime-render run mvp");

  EXPECT(run.ok, "realtime-render run should succeed with a hook");
  EXPECT(hookCalled, "realtime-render run should call the generate hook");
  EXPECT(capturedScene == session.scene(),
         "generate hook should receive the current scene");
  EXPECT(capturedRequest.sceneName == "IBL Metal Sphere",
         "generate hook should receive the scene name");
  EXPECT(capturedRequest.profileName == "mvp",
         "generate hook should receive the selected profile name");
  EXPECT(capturedRequest.output.width == 1024 &&
             capturedRequest.output.height == 576,
         "generate hook should receive the resolved output profile");
  EXPECT(capturedRequest.outputBasePath.generic_string() ==
             "artifacts/offline/mvp/realtime/IBL%20Metal%20Sphere/mvp/render",
         "generate hook should receive the stable realtime output base path");
  EXPECT(run.structured.find("\"profile\":\"mvp\"") != std::string::npos,
         "run JSON should include the profile name");
  EXPECT(run.structured.find("\"width\":1024") != std::string::npos,
         "run JSON should include width");
  EXPECT(run.structured.find("linear.exr") != std::string::npos,
         "run JSON should include linear EXR path");
  EXPECT(run.structured.find("cpu.png") != std::string::npos,
         "run JSON should include CPU sRGB PNG path");
  EXPECT(run.structured.find("pipeline.png") != std::string::npos,
         "run JSON should include pipeline sRGB PNG path");
  EXPECT(run.structured.find("render.json") != std::string::npos,
         "run JSON should include metadata path");

  cleanupProject("editor_session_realtime_run");
}

void testRealtimeRenderRunFailsWithoutHook() {
  const bool initialized = initializeRuntimeAssetRoot();
  EXPECT(initialized,
         "runtime asset root should initialize for realtime no-hook test");
  if (!initialized) {
    return;
  }
  cleanupProject("editor_session_realtime_no_hook");

  LX_core::EditorState editorState;
  LX_demo::lxe_editor::CameraRig rig;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();

  EXPECT(session.commandBus()
             .dispatch("project init pbr_ibl editor_session_realtime_no_hook")
             .ok,
         "project init should load a scene with output profiles");

  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  session.flushPendingSceneOpen(loop);

  const auto run = session.commandBus().dispatch("realtime-render run preview");

  EXPECT(!run.ok, "realtime-render run should fail without a hook");
  EXPECT(run.message.find("realtime render output hook unavailable") !=
             std::string::npos,
         "run failure should explain that no output hook is configured");

  cleanupProject("editor_session_realtime_no_hook");
}

} // namespace

int main() {
  testProjectSceneOpenPreservesEditorCommandHistoryAndConsole();
  testSceneOpenFailureKeepsEditorRunningAndCurrentScene();
  testStartupClosesProjectWhenLastProjectSceneCannotLoad();
  testStartupWithoutLastProjectOpensDefaultProject();
  testSceneOpenClearsSelectionAndDebugDrawState();
  testProjectCloseCancelsPendingSceneOpen();
  testProjectCloseAfterLoadedSceneUpdatesEngineLoop();
  testProjectListMessageIncludesProjectIdsAndPaths();
  testProjectSaveMessageIncludesProjectAndScenePaths();
  testEditorDoesNotCreateCameraOrLightHelperNodes();
  testRecordingCommandControlsSessionRecorder();
  testSceneSaveLoadRoundTripsEditorSidecarState();
  testRenderDebugDumpCommandUsesRegisteredHook();
  testRealtimeRenderListUsesCurrentSceneOutputProfiles();
  testRealtimeRenderListUsesDefaultProfilesForSceneWithoutProfiles();
  testRealtimeRenderRunCallsHookAndReturnsStructuredOutput();
  testRealtimeRenderRunFailsWithoutHook();

  if (failures != 0) {
    std::cerr << failures << " lxe_editor session test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor session tests\n";
  return 0;
}
