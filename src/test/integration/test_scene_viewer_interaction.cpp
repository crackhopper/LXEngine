#include "core/asset/mesh.hpp"
#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/input/mock_input_state.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "demos/scene_viewer/scene_interaction_controller.hpp"
#include "demos/scene_viewer/scene_input_routing.hpp"

#include <iostream>

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

LX_core::MeshSharedPtr makeUnitSquareMesh() {
  auto vb = LX_core::VertexBuffer<LX_core::VertexPos>::create(
      std::vector<LX_core::VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = LX_core::IndexBuffer::create({0, 1, 2});
  return LX_core::Mesh::create(vb, ib, LX_core::BoundingBox{{0, 0, 0}, {1, 1, 0}});
}

struct Fixture final {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::SceneSharedPtr scene = LX_core::Scene::create(nullptr);
  LX_core::SceneNodeSharedPtr editorCameraNode =
      LX_core::SceneNode::create("editor_cam_node");
  LX_core::SceneNodeSharedPtr gameCameraNode =
      LX_core::SceneNode::create("game_cam_node");
  LX_core::SceneNodeSharedPtr targetNode = LX_core::SceneNode::create("cube");
  LX_demo::scene_viewer::SceneInteractionController controller;

  Fixture()
      : controller(bus, editorState, *scene) {
    targetNode->setName("cube");
    targetNode->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    targetNode->setTranslation({-0.5f, -0.5f, -5.0f});
    scene->addRenderable(targetNode);

    editorCameraNode->setName("editor_cam");
    auto editorCamera =
        editorCameraNode->addComponent<LX_core::CameraComponent>();
    editorCamera->get().aspect = 1.0f;
    scene->addCamera(editorCameraNode);

    gameCameraNode->setName("game_cam");
    gameCameraNode->addComponent<LX_core::CameraComponent>();
    scene->addCamera(gameCameraNode);

    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(gameCameraNode);
    (void)editorState.syncActiveCamera(*scene);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
  }
};

void runSceneViewerMainPathSelectionStep(Fixture& fixture,
                                         LX_core::MockInputState& input,
                                         const bool wantsKeyboard,
                                         const bool wantsMouse,
                                         const bool selectionModeActive) {
  if (LX_demo::scene_viewer::shouldProcessSelectionMode(
          fixture.editorState.isPreviewEnabled(), wantsMouse,
          selectionModeActive
              ? LX_demo::scene_viewer::SceneInputEditMode::Selection
              : LX_demo::scene_viewer::SceneInputEditMode::Orbit)) {
    fixture.controller.updateSelectionMode(input, {800.0f, 600.0f});
  }
  EXPECT(!LX_demo::scene_viewer::shouldProcessCameraRig(
             fixture.editorState.isPreviewEnabled(), wantsKeyboard, wantsMouse,
             LX_demo::scene_viewer::SceneInputEditMode::Selection),
         "selection mode test harness should not route into the camera rig branch");
}

void testSceneInteractionSelectsHitNodeOnClick() {
  Fixture fixture;
  const auto result =
      fixture.controller.dispatchPickingClick({400.0f, 300.0f}, {800.0f, 600.0f});
  EXPECT(result.ok, "selection click should succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1, "selection click should pick one node");
  EXPECT(selected.size() == 1 && selected.front() == fixture.targetNode,
         "selection click should pick the mesh under the cursor");
}

void testSceneInteractionDeselectsOnMiss() {
  Fixture fixture;
  fixture.editorState.select({fixture.targetNode});
  const auto result =
      fixture.controller.dispatchPickingClick({799.0f, 0.0f}, {800.0f, 600.0f});
  EXPECT(result.ok, "miss click should still dispatch");
  EXPECT(fixture.editorState.getSelected().empty(),
         "miss click should deselect");
}

void testSelectionModeConsumesOnlyLeftPressEdge() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});

  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  fixture.controller.updateSelectionMode(input, {800.0f, 600.0f});
  EXPECT(fixture.bus.history().size() == 1,
         "first left press should dispatch one selection command");

  fixture.controller.updateSelectionMode(input, {800.0f, 600.0f});
  EXPECT(fixture.bus.history().size() == 1,
         "held left button should not redispatch selection");

  input.setMouseButtonDown(LX_core::MouseButton::Left, false);
  fixture.controller.updateSelectionMode(input, {800.0f, 600.0f});
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  fixture.controller.updateSelectionMode(input, {800.0f, 600.0f});
  EXPECT(fixture.bus.history().size() == 2,
         "second left press edge should dispatch again");
}

void testPreviewModeSuppressesSelectionInMainPath() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);

  fixture.editorState.setPreviewEnabled(true);
  runSceneViewerMainPathSelectionStep(fixture, input, false, false, true);

  EXPECT(fixture.editorState.getSelected().empty(),
         "preview mode should suppress selection clicks in the scene_viewer main path");
  EXPECT(fixture.bus.history().empty(),
         "preview mode should not dispatch selection commands");
}

void testSelectionModeAllowsMousePickingWhileKeyboardIsCaptured() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);

  runSceneViewerMainPathSelectionStep(fixture, input, true, false, true);

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1 && selected.front() == fixture.targetNode,
         "selection mode should still pick when only keyboard is captured elsewhere");
}

void testSelectionDebugStateTracksHitPointAndSelection() {
  Fixture fixture;
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();

  const auto result =
      fixture.controller.dispatchPickingClick({400.0f, 300.0f}, {800.0f, 600.0f});
  EXPECT(result.ok, "selection click should succeed before drawing debug state");

  fixture.controller.enqueueDebugDraw();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() > 0,
         "selection debug draw should emit AABB and hit marker geometry");
  const auto marker = fixture.controller.lastHitPoint();
  EXPECT(marker.has_value(), "successful selection click should record hit point");

  const auto miss =
      fixture.controller.dispatchPickingClick({799.0f, 0.0f}, {800.0f, 600.0f});
  EXPECT(miss.ok, "miss click should still dispatch before clearing debug state");
  EXPECT(!fixture.controller.lastHitPoint().has_value(),
         "miss click should clear the stored hit point");

  LX_core::DebugDraw::endFrame();
}

} // namespace

int main() {
  expSetEnvVK();
  testSceneInteractionSelectsHitNodeOnClick();
  testSceneInteractionDeselectsOnMiss();
  testSelectionModeConsumesOnlyLeftPressEdge();
  testPreviewModeSuppressesSelectionInMainPath();
  testSelectionModeAllowsMousePickingWhileKeyboardIsCaptured();
  testSelectionDebugStateTracksHitPointAndSelection();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: scene_viewer interaction tests passed\n";
  return 0;
}
