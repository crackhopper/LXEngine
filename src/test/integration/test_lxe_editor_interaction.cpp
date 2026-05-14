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
#include "core/scene/light.hpp"
#include "core/utils/env.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/scene_input_routing.hpp"
#include "demos/lxe_editor/scene_view_rect.hpp"
#include "demos/lxe_editor/selection_camera_input.hpp"

#include <cmath>
#include <iostream>
#include <optional>
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

constexpr float kEps = 2e-3f;

[[nodiscard]] bool approx(const float a, const float b,
                          const float eps = kEps) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] bool approx(const LX_core::Vec2f& a, const LX_core::Vec2f& b,
                          const float eps = kEps) {
  return approx(a.x, b.x, eps) && approx(a.y, b.y, eps);
}

[[nodiscard]] std::optional<LX_core::Vec2f> projectWorldPointToViewport(
    const LX_core::Vec3f& worldPoint, LX_core::CameraComponent& camera,
    const LX_core::Vec2f& viewportSize) {
  const LX_core::Mat4f viewProj =
      camera.getProjMatrix() * camera.getViewMatrix();
  const LX_core::Vec4f clip =
      viewProj * LX_core::Vec4f{worldPoint.x, worldPoint.y, worldPoint.z, 1.0f};
  if (std::abs(clip.w) <= 1e-6f || clip.w <= 0.0f) {
    return std::nullopt;
  }
  const LX_core::Vec3f ndc = clip.toVec3();
  return LX_core::Vec2f{
      (ndc.x * 0.5f + 0.5f) * viewportSize.x - 0.5f,
      (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y - 0.5f};
}

[[nodiscard]] std::optional<float> extractJsonFloatField(
    const std::string& text, const std::string& anchor,
    const std::string& fieldName) {
  const std::size_t anchorStart = text.find(anchor);
  if (anchorStart == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t fieldStart = text.find(fieldName, anchorStart + anchor.size());
  if (fieldStart == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t valueStart = fieldStart + fieldName.size();
  std::size_t valueEnd = valueStart;
  while (valueEnd < text.size()) {
    const char ch = text[valueEnd];
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
      ++valueEnd;
      continue;
    }
    break;
  }
  if (valueEnd == valueStart) {
    return std::nullopt;
  }
  return std::stof(text.substr(valueStart, valueEnd - valueStart));
}

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
  LX_core::SceneNodeSharedPtr lightNode = LX_core::SceneNode::create("dir_light_node");
  LX_demo::lxe_editor::SceneInteractionController controller;

  Fixture()
      : controller(bus, editorState, *scene) {
    targetNode->setName("cube");
    targetNode->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    targetNode->setTranslation({-0.5f, -0.5f, -5.0f});
    scene->addRenderable(targetNode);

    editorCameraNode->setName("editor_cam");
    auto editorCamera =
        editorCameraNode->addComponent<LX_core::CameraComponent>();
    editorCamera->get().setAspect(1.0f);
    scene->addCamera(editorCameraNode);

    gameCameraNode->setName("game_cam");
    gameCameraNode->addComponent<LX_core::CameraComponent>();
    scene->addCamera(gameCameraNode);

    lightNode->setName("dir_light");
    scene->addRenderable(lightNode);
    scene->attachLight(lightNode, std::make_shared<LX_core::DirectionalLight>());

    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(gameCameraNode);
    (void)editorState.syncActiveCamera(*scene);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testPickRayProjectionRoundTripsBackToOriginalViewportPixel() {
  Fixture fixture;
  const auto editorCamera =
      fixture.editorCameraNode->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (!editorCamera.has_value()) {
    return;
  }

  editorCamera->get().lookAt({1.5f, 1.2f, 4.0f}, {0.25f, 0.5f, 0.0f},
                             {0.0f, 1.0f, 0.0f});
  editorCamera->get().setAspect(1920.0f / 1008.0f);
  const LX_core::Vec2f viewportSize{1920.0f, 1008.0f};
  const LX_core::Vec2f screenPixel{1097.0f, 341.0f};

  const LX_core::Ray ray = editorCamera->get().pickRay(screenPixel, viewportSize);
  const LX_core::Vec3f worldPoint = ray.origin + ray.direction * 5.0f;
  const auto projected =
      projectWorldPointToViewport(worldPoint, editorCamera->get(), viewportSize);
  EXPECT(projected.has_value(),
         "point on pick ray should remain projectable through the same camera");
  if (!projected.has_value()) {
    return;
  }

  if (!(approx(projected->x, screenPixel.x) &&
        approx(projected->y, screenPixel.y))) {
    std::cerr << "  projected=(" << projected->x << ", " << projected->y
              << ") expected=(" << screenPixel.x << ", " << screenPixel.y
              << ")\n";
  }
  EXPECT(approx(projected->x, screenPixel.x) &&
             approx(projected->y, screenPixel.y),
         "point sampled from pick ray should project back to the original pixel");
}

void runSceneViewerMainPathSelectionStep(Fixture& fixture,
                                         LX_core::MockInputState& input,
                                         const bool wantsKeyboard,
                                         const bool wantsMouse,
                                         const bool gizmoConsumesMouse) {
  if (LX_demo::lxe_editor::shouldProcessSelectionMode(
          fixture.editorState.isPreviewEnabled(), wantsMouse,
          gizmoConsumesMouse,
          LX_demo::lxe_editor::SceneInputEditMode::Selection)) {
    fixture.controller.updateSelectionMode(
        input, LX_core::Vec2f{800.0f, 600.0f});
  } else {
    fixture.controller.cancelPendingSelectionClick(input);
  }
  (void)wantsKeyboard;
}

void testSceneInteractionSelectsHitNodeOnClick() {
  Fixture fixture;
  const auto result =
      fixture.controller.dispatchPickingClick(LX_core::Vec2f{400.0f, 300.0f},
                                             LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(result.ok, "selection click should succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1, "selection click should pick one node");
  EXPECT(selected.size() == 1 && selected.front() == fixture.targetNode,
         "selection click should pick the mesh under the cursor");
}

void testSceneInteractionSelectsLightNodeViaDebugBounds() {
  Fixture fixture;
  fixture.targetNode->setTranslation({100.0f, 100.0f, -5.0f});
  fixture.lightNode->setTranslation({0.0f, 0.0f, -5.0f});

  const auto result =
      fixture.controller.dispatchPickingClick(LX_core::Vec2f{400.0f, 300.0f},
                                             LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(result.ok, "light debug bounds should be pickable");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1 && selected.front() == fixture.lightNode,
         "selection click should pick the light owner node");
}

void testSceneInteractionDeselectsOnMiss() {
  Fixture fixture;
  fixture.editorState.select({fixture.targetNode});
  const auto result =
      fixture.controller.dispatchPickingClick(LX_core::Vec2f{799.0f, 0.0f},
                                             LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(result.ok, "miss click should still dispatch");
  EXPECT(fixture.editorState.getSelected().empty(),
         "miss click should deselect");
}

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

void testSelectionPickingIgnoresClicksOutsideSceneViewRect() {
  Fixture fixture;
  const LX_demo::lxe_editor::SceneViewRect rect{
      .x = 280.0f,
      .y = 68.0f,
      .width = 640.0f,
      .height = 432.0f,
  };

  const auto result =
      fixture.controller.dispatchPickingClick({32.0f, 32.0f}, rect);
  EXPECT(!result.ok,
         "clicks outside the scene view rect should not dispatch scene picks");
  EXPECT(fixture.editorState.getSelected().empty(),
         "outside-rect clicks should leave selection untouched");
}

void testSelectionModeDispatchesPickOnLeftRelease() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});

  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  fixture.controller.updateSelectionMode(input, LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(fixture.bus.history().empty(),
         "left press should arm selection without dispatching immediately");

  fixture.controller.updateSelectionMode(input, LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(fixture.bus.history().empty(),
         "held left button should not dispatch selection");

  input.setMouseButtonDown(LX_core::MouseButton::Left, false);
  fixture.controller.updateSelectionMode(input, LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(fixture.bus.history().size() == 1,
         "left release should dispatch one selection command");

  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  fixture.controller.updateSelectionMode(input, LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(fixture.bus.history().size() == 1,
         "second left press should arm without dispatching");
  input.setMouseButtonDown(LX_core::MouseButton::Left, false);
  fixture.controller.updateSelectionMode(input, LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(fixture.bus.history().size() == 2,
         "second left release should dispatch again");
}

void testSuppressedSelectionFrameCancelsArmedClick() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});

  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  runSceneViewerMainPathSelectionStep(fixture, input, false, false, false);
  EXPECT(fixture.bus.history().empty(),
         "allowed press should only arm selection");

  runSceneViewerMainPathSelectionStep(fixture, input, false, false, true);
  input.setMouseButtonDown(LX_core::MouseButton::Left, false);
  runSceneViewerMainPathSelectionStep(fixture, input, false, false, false);

  EXPECT(fixture.bus.history().empty(),
         "suppressed frame should cancel the armed press instead of leaking a stale pick");
  EXPECT(fixture.editorState.getSelected().empty(),
         "suppressed click should leave selection unchanged");
}

void testSelectionDragDispatchesBoxSelectionOnRelease() {
  Fixture fixture;
  LX_core::MockInputState input;
  bool boxSelectionDispatched = false;
  LX_core::Vec2f capturedStart{0.0f, 0.0f};
  LX_core::Vec2f capturedEnd{0.0f, 0.0f};
  bool capturedCtrl = false;
  bool capturedShift = false;
  fixture.controller.setBoxSelectionDispatch(
      [&](const LX_core::Vec2f& dragStart, const LX_core::Vec2f& dragEnd,
          const LX_demo::lxe_editor::SceneViewRect& sceneViewRect,
          const bool ctrlHeld, const bool shiftHeld) {
        boxSelectionDispatched = true;
        capturedStart = sceneViewRect.localPixel(dragStart);
        capturedEnd = sceneViewRect.localPixel(dragEnd);
        capturedCtrl = ctrlHeld;
        capturedShift = shiftHeld;
        return LX_core::CommandResult{true, "box selection dispatched", {}, {}};
      });

  input.setMousePosition({300.0f, 200.0f});
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  fixture.controller.updateSelectionMode(
      input, LX_demo::lxe_editor::SceneViewRect{
                 .x = 100.0f, .y = 50.0f, .width = 800.0f, .height = 600.0f});

  input.setMousePosition({520.0f, 420.0f});
  input.setKeyDown(LX_core::KeyCode::LCtrl, true);
  input.setKeyDown(LX_core::KeyCode::LShift, true);
  fixture.controller.updateSelectionMode(
      input, LX_demo::lxe_editor::SceneViewRect{
                 .x = 100.0f, .y = 50.0f, .width = 800.0f, .height = 600.0f});
  EXPECT(!boxSelectionDispatched,
         "held drag should not dispatch box selection before release");

  input.setMouseButtonDown(LX_core::MouseButton::Left, false);
  fixture.controller.updateSelectionMode(
      input, LX_demo::lxe_editor::SceneViewRect{
                 .x = 100.0f, .y = 50.0f, .width = 800.0f, .height = 600.0f});

  EXPECT(boxSelectionDispatched,
         "left drag release should dispatch box selection");
  EXPECT(capturedStart.x == 200.0f && capturedStart.y == 150.0f &&
             capturedEnd.x == 420.0f && capturedEnd.y == 370.0f,
         "box selection should receive scene-local drag pixels");
  EXPECT(capturedCtrl && capturedShift,
         "box selection should receive append modifier state");
  EXPECT(fixture.bus.history().empty(),
         "box selection dispatcher should replace click picking for drags");
}

void testPreviewModeSuppressesSelectionInMainPath() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);

  fixture.editorState.setPreviewEnabled(true);
  runSceneViewerMainPathSelectionStep(fixture, input, false, false, false);

  EXPECT(fixture.editorState.getSelected().empty(),
         "preview mode should suppress selection clicks in the lxe_editor main path");
  EXPECT(fixture.bus.history().empty(),
         "preview mode should not dispatch selection commands");
}

void testSelectionModeAllowsMousePickingWhileKeyboardIsCaptured() {
  Fixture fixture;
  LX_core::MockInputState input;
  input.setMousePosition({400.0f, 300.0f});
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);

  runSceneViewerMainPathSelectionStep(fixture, input, true, false, false);

  input.setMouseButtonDown(LX_core::MouseButton::Left, false);
  runSceneViewerMainPathSelectionStep(fixture, input, true, false, false);

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1 && selected.front() == fixture.targetNode,
         "selection mode should still pick when only keyboard is captured elsewhere");
}

void testSelectionModeStillAllowsCameraRigRouting() {
  EXPECT(LX_demo::lxe_editor::shouldProcessSelectionMode(
             false, false, false,
             LX_demo::lxe_editor::SceneInputEditMode::Selection),
         "selection mode should still process selection clicks");
  EXPECT(LX_demo::lxe_editor::shouldProcessCameraRig(
             false, false, false, false),
         "selection mode should still allow camera rig updates");
}

void testSelectionRoutingIsSuppressedByGizmoMouse() {
  EXPECT(!LX_demo::lxe_editor::shouldProcessSelectionMode(
             false, false, true,
             LX_demo::lxe_editor::SceneInputEditMode::Selection),
         "gizmo hover/use should suppress selection routing");
}

void testCameraRoutingIsSuppressedByGizmoMouse() {
  EXPECT(!LX_demo::lxe_editor::shouldProcessCameraRig(
             false, false, false, true),
         "gizmo hover/use should suppress camera rig routing");
}

void testCameraRoutingRunsInSelectionModeWithoutPreviewOrUiCapture() {
  EXPECT(LX_demo::lxe_editor::shouldProcessCameraRig(
             false, false, false, false),
         "camera rig should run in selection mode when preview and UI capture are off");
}

void testPreviewSuppressesSelectionAndCameraRouting() {
  EXPECT(!LX_demo::lxe_editor::shouldProcessSelectionMode(
             true, false, false,
             LX_demo::lxe_editor::SceneInputEditMode::Selection),
         "preview should suppress selection routing");
  EXPECT(!LX_demo::lxe_editor::shouldProcessCameraRig(
             true, false, false, false),
         "preview should suppress camera rig routing");
}

void testSelectionCameraInputOrbitMapsRightMouseToLeftOnly() {
  LX_core::MockInputState input;
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  input.setMouseDelta({3.0f, -2.0f});

  LX_demo::lxe_editor::SelectionCameraInput cameraInput(
      input, LX_demo::lxe_editor::SelectionNavigationMode::Orbit);
  EXPECT(!cameraInput.isMouseButtonDown(LX_core::MouseButton::Left),
         "orbit adapter should not expose raw left mouse");
  EXPECT(!cameraInput.isMouseButtonDown(LX_core::MouseButton::Right),
         "orbit adapter should suppress raw right mouse when not held");
  EXPECT(approx(cameraInput.getMouseDelta(), LX_core::Vec2f{0.0f, 0.0f}),
         "orbit adapter should suppress mouse delta until right is held");

  input.setMouseButtonDown(LX_core::MouseButton::Right, true);
  EXPECT(cameraInput.isMouseButtonDown(LX_core::MouseButton::Left),
         "orbit adapter should map right-held to left mouse");
  EXPECT(!cameraInput.isMouseButtonDown(LX_core::MouseButton::Right),
         "orbit adapter should still suppress raw right mouse");
  EXPECT(approx(cameraInput.getMouseDelta(), LX_core::Vec2f{3.0f, -2.0f}),
         "orbit adapter should expose mouse delta while right is held");
}

void testSelectionCameraInputFreeFlyExposesRightMouseAndKeyboardOnlyWhileHeld() {
  LX_core::MockInputState input;
  input.setKeyDown(LX_core::KeyCode::W, true);
  input.setMouseButtonDown(LX_core::MouseButton::Left, true);
  input.setMouseDelta({1.0f, 2.0f});

  LX_demo::lxe_editor::SelectionCameraInput cameraInput(
      input, LX_demo::lxe_editor::SelectionNavigationMode::FreeFly);
  EXPECT(!cameraInput.isKeyDown(LX_core::KeyCode::W),
         "freefly adapter should suppress keyboard until right is held");
  EXPECT(!cameraInput.isMouseButtonDown(LX_core::MouseButton::Left),
         "freefly adapter should not expose raw left mouse");
  EXPECT(!cameraInput.isMouseButtonDown(LX_core::MouseButton::Right),
         "freefly adapter should suppress right mouse until held");
  EXPECT(approx(cameraInput.getMouseDelta(), LX_core::Vec2f{0.0f, 0.0f}),
         "freefly adapter should suppress mouse delta until right is held");

  input.setMouseButtonDown(LX_core::MouseButton::Right, true);
  EXPECT(cameraInput.isKeyDown(LX_core::KeyCode::W),
         "freefly adapter should expose keyboard while right is held");
  EXPECT(!cameraInput.isMouseButtonDown(LX_core::MouseButton::Left),
         "freefly adapter should never expose raw left mouse");
  EXPECT(cameraInput.isMouseButtonDown(LX_core::MouseButton::Right),
         "freefly adapter should expose right mouse while held");
  EXPECT(approx(cameraInput.getMouseDelta(), LX_core::Vec2f{1.0f, 2.0f}),
         "freefly adapter should expose mouse delta while right is held");
}

void testSelectionDebugStateTracksHitPointAndSelection() {
  Fixture fixture;
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();

  const auto result =
      fixture.controller.dispatchPickingClick(LX_core::Vec2f{400.0f, 300.0f},
                                             LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(result.ok, "selection click should succeed before drawing debug state");

  fixture.controller.enqueueDebugDraw();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() > 0,
         "selection debug draw should emit AABB and hit marker geometry");
  const auto marker = fixture.controller.lastHitPoint();
  EXPECT(marker.has_value(), "successful selection click should record hit point");

  const auto miss =
      fixture.controller.dispatchPickingClick(LX_core::Vec2f{799.0f, 0.0f},
                                             LX_core::Vec2f{800.0f, 600.0f});
  EXPECT(miss.ok, "miss click should still dispatch before clearing debug state");
  EXPECT(!fixture.controller.lastHitPoint().has_value(),
         "miss click should clear the stored hit point");

  LX_core::DebugDraw::endFrame();
}

void testSelectionDebugProjectionRoundTripsBackToClickedPixel() {
  Fixture fixture;
  std::string debugLine;
  fixture.controller.setDebugLoggingHooks(
      []() { return true; },
      [&debugLine](std::string_view line) { debugLine = std::string(line); });

  const auto editorCamera =
      fixture.editorCameraNode->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (!editorCamera.has_value()) {
    return;
  }

  const LX_demo::lxe_editor::SceneViewRect rect{
      .x = 0.0f,
      .y = 0.0f,
      .width = 1920.0f,
      .height = 1008.0f,
  };
  fixture.targetNode->setScale({6.0f, 6.0f, 1.0f});
  editorCamera->get().lookAt({1.5f, 1.2f, 4.0f}, {0.25f, 0.5f, 0.0f},
                             {0.0f, 1.0f, 0.0f});
  editorCamera->get().setAspect(rect.width / rect.height);

  const auto projectedTargetCenter = projectWorldPointToViewport(
      LX_core::Vec3f{2.5f, 2.5f, -5.0f}, editorCamera->get(), rect.size());
  EXPECT(projectedTargetCenter.has_value(),
         "target center should be projectable for debug round-trip test");
  if (!projectedTargetCenter.has_value()) {
    return;
  }

  const auto result = fixture.controller.dispatchPickingClick(
      LX_core::Vec2f{projectedTargetCenter->x, projectedTargetCenter->y}, rect);
  EXPECT(result.ok, "projected target-center click should succeed");

  const auto projectedX = extractJsonFloatField(
      debugLine, "\"projectedPixel\":{", "\"x\":");
  const auto projectedY = extractJsonFloatField(
      debugLine, "\"projectedPixel\":{", "\"y\":");
  if (!(projectedX.has_value() && projectedY.has_value() &&
        approx(*projectedX, projectedTargetCenter->x, 1e-2f) &&
        approx(*projectedY, projectedTargetCenter->y, 1e-2f))) {
    std::cerr << "  debugLine=" << debugLine << "\n";
  }
  EXPECT(projectedX.has_value() && projectedY.has_value() &&
             approx(*projectedX, projectedTargetCenter->x, 1e-2f) &&
             approx(*projectedY, projectedTargetCenter->y, 1e-2f),
         "debug reprojection should match the clicked pixel for a hit on the pick ray");
}

void testSelectionDebugUsesNegativeNdcYForLowerScreenPixels() {
  Fixture fixture;
  std::string debugLine;
  fixture.controller.setDebugLoggingHooks(
      []() { return true; },
      [&debugLine](std::string_view line) { debugLine = std::string(line); });

  const auto editorCamera =
      fixture.editorCameraNode->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (!editorCamera.has_value()) {
    return;
  }

  const LX_demo::lxe_editor::SceneViewRect rect{
      .x = 0.0f,
      .y = 0.0f,
      .width = 1920.0f,
      .height = 1008.0f,
  };
  fixture.targetNode->setScale({6.0f, 6.0f, 1.0f});
  editorCamera->get().lookAt({1.5f, 1.2f, 4.0f}, {0.25f, 0.5f, 0.0f},
                             {0.0f, 1.0f, 0.0f});
  editorCamera->get().setAspect(rect.width / rect.height);

  const LX_core::Vec2f lowerScreenPixel{901.0f, 896.0f};
  const auto result =
      fixture.controller.dispatchPickingClick(lowerScreenPixel, rect);
  EXPECT(result.ok, "lower-screen selection click should succeed");
  const auto screenNdcX = extractJsonFloatField(
      debugLine, "\"screenNdc\":{", "\"x\":");
  const auto screenNdcY = extractJsonFloatField(
      debugLine, "\"screenNdc\":{", "\"y\":");
  EXPECT(screenNdcX.has_value(),
         "debug line should include the screen NDC x component");
  EXPECT(screenNdcY.has_value(),
         "debug line should include the screen NDC y component");
  if (!(screenNdcY.has_value() && *screenNdcY < 0.0f)) {
    std::cerr << "  debugLine=" << debugLine << "\n";
  }
  EXPECT(screenNdcY.has_value() && *screenNdcY < 0.0f,
         "lower-screen pixels should map to negative top-left-origin NDC Y");
}

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

void testEditorModeDrawsCameraAndLightDebugHelpers() {
  Fixture fixture;
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();

  fixture.controller.enqueueDebugDraw();

  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() >= 15,
         "editor mode should draw camera frustum and directional-light debug lines");
  LX_core::DebugDraw::endFrame();
}

void testPreviewModeSuppressesEditorDebugHelpers() {
  Fixture fixture;
  fixture.editorState.setPreviewEnabled(true);
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();

  fixture.controller.enqueueDebugDraw();

  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() == 0,
         "preview mode should suppress editor helper debug draw");
  LX_core::DebugDraw::endFrame();
}

} // namespace

int main() {
  expSetEnvVK();
  testPickRayProjectionRoundTripsBackToOriginalViewportPixel();
  testSceneInteractionSelectsHitNodeOnClick();
  testSceneInteractionSelectsLightNodeViaDebugBounds();
  testSceneInteractionDeselectsOnMiss();
  testSelectionPickingUsesSceneViewRectInsteadOfWholeWindow();
  testSelectionPickingIgnoresClicksOutsideSceneViewRect();
  testSelectionModeDispatchesPickOnLeftRelease();
  testSuppressedSelectionFrameCancelsArmedClick();
  testSelectionDragDispatchesBoxSelectionOnRelease();
  testPreviewModeSuppressesSelectionInMainPath();
  testSelectionModeAllowsMousePickingWhileKeyboardIsCaptured();
  testSelectionModeStillAllowsCameraRigRouting();
  testSelectionRoutingIsSuppressedByGizmoMouse();
  testCameraRoutingIsSuppressedByGizmoMouse();
  testCameraRoutingRunsInSelectionModeWithoutPreviewOrUiCapture();
  testPreviewSuppressesSelectionAndCameraRouting();
  testSelectionCameraInputOrbitMapsRightMouseToLeftOnly();
  testSelectionCameraInputFreeFlyExposesRightMouseAndKeyboardOnlyWhileHeld();
  testSelectionDebugStateTracksHitPointAndSelection();
  testSelectionDebugProjectionRoundTripsBackToClickedPixel();
  testSelectionDebugUsesNegativeNdcYForLowerScreenPixels();
  testResetEditorCameraToGameCameraCopiesPoseWithoutPreviewToggle();
  testEditorModeDrawsCameraAndLightDebugHelpers();
  testPreviewModeSuppressesEditorDebugHelpers();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: lxe_editor interaction tests passed\n";
  return 0;
}
