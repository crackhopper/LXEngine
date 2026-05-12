#include "demos/lxe_editor/editor_session.hpp"

#include "core/editor/console_panel.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/input/dummy_input_state.hpp"
#include "core/rhi/renderer.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <iostream>
#include <memory>
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
  }
  void uploadData() override {}
  void draw() override {}

  int initSceneCalls = 0;
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

  requireHelperVertexCount("/game_cam/helper_camera");
  requireHelperVertexCount("/dir_light/helper_light");
}

} // namespace

int main() {
  testSceneLoadPreservesEditorCommandHistoryAndConsole();
  testEditorHelpersUseFacetedOctahedronGeometry();

  if (failures != 0) {
    std::cerr << failures << " lxe_editor session test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor session tests\n";
  return 0;
}
