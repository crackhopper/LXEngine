#include "core/asset/mesh.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_config.hpp"
#include "core/editor/editor_state.hpp"
#define private public
#include "core/editor/viewport_overlay.hpp"
#undef private
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
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

LX_core::MeshSharedPtr makeUnitSquareMesh() {
  auto vb = LX_core::VertexBuffer<LX_core::VertexPos>::create(
      std::vector<LX_core::VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = LX_core::IndexBuffer::create({0, 1, 2});
  return LX_core::Mesh::create(vb, ib,
                               LX_core::BoundingBox{{0, 0, 0}, {1, 1, 0}});
}

struct Fixture {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::SceneSharedPtr scene = LX_core::Scene::create(nullptr);
  LX_core::SceneNodeSharedPtr world = LX_core::SceneNode::create("world");
  LX_core::SceneNodeSharedPtr editorCameraNode =
      LX_core::SceneNode::create("editor_cam_node");
  LX_core::SceneNodeSharedPtr gameCameraNode =
      LX_core::SceneNode::create("game_cam_node");
  LX_core::SceneNodeSharedPtr cube = LX_core::SceneNode::create("cube");
  LX_core::SceneNodeSharedPtr sphere = LX_core::SceneNode::create("sphere");
  LX_core::SceneNodeSharedPtr cone = LX_core::SceneNode::create("cone");
  LX_core::CameraComponent *editorCamera = nullptr;
  LX_core::CameraComponent *gameCamera = nullptr;

  Fixture() {
    world->setName("world");
    cube->setName("cube");
    cube->setParent(world);
    sphere->setName("sphere");
    sphere->setParent(world);
    cone->setName("cone");
    cone->setParent(world);
    cube->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    sphere->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    cone->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    cube->setTranslation({-1.5f, -0.5f, -5.0f});
    sphere->setTranslation({0.0f, -0.5f, -5.0f});
    cone->setTranslation({1.5f, -0.5f, -5.0f});
    scene->addRenderable(world);
    scene->addRenderable(cube);
    scene->addRenderable(sphere);
    scene->addRenderable(cone);

    editorCameraNode->setName("editor_cam");
    auto editorCamRef =
        editorCameraNode->addComponent<LX_core::CameraComponent>();
    editorCamera = &editorCamRef->get();
    editorCamera->setAspect(1.0f);
    scene->addCamera(editorCameraNode);

    gameCameraNode->setName("game_cam");
    auto gameCamRef = gameCameraNode->addComponent<LX_core::CameraComponent>();
    gameCamera = &gameCamRef->get();
    scene->addCamera(gameCameraNode);

    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(gameCameraNode);
    (void)editorState.syncActiveCamera(*scene);
    registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testEditorStateSyncsActiveCameraAcrossPreviewToggle() {
  Fixture fixture;

  EXPECT(fixture.editorCamera->isActive(), "editor camera active by default");
  EXPECT(!fixture.gameCamera->isActive(), "preview camera inactive by default");

  const auto onResult = fixture.bus.dispatch("preview on");
  EXPECT(onResult.ok, "preview on command succeeds");
  EXPECT(fixture.editorState.isPreviewEnabled(), "preview state flips on");
  EXPECT(!fixture.editorCamera->isActive(),
         "editor camera deactivates in preview");
  EXPECT(fixture.gameCamera->isActive(), "preview camera activates in preview");
  EXPECT(onResult.structured.find("\"activePath\":\"/game_cam\"") !=
             std::string::npos,
         "preview structured payload includes active camera path");

  const auto offResult = fixture.bus.dispatch("preview off");
  EXPECT(offResult.ok, "preview off command succeeds");
  EXPECT(!fixture.editorState.isPreviewEnabled(), "preview state flips off");
  EXPECT(fixture.editorCamera->isActive(), "editor camera reactivates");
  EXPECT(!fixture.gameCamera->isActive(), "preview camera deactivates");
}

void testViewportOverlaySnapshotAndCommandEntry() {
  Fixture fixture;
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  const auto snapshot = overlay.makeSnapshot();
  EXPECT(snapshot.editorCameraPath == "/editor_cam",
         "snapshot keeps editor camera path");
  EXPECT(snapshot.previewCameraPath == "/game_cam",
         "snapshot keeps preview camera path");
  EXPECT(snapshot.activeCameraPath == "/editor_cam",
         "snapshot reports current active camera");
  EXPECT(snapshot.selectedPath == "/world/cube",
         "snapshot reports selected node");
  EXPECT(snapshot.hintText.find("F preview: OFF") != std::string::npos,
         "snapshot hint text includes preview status");

  const auto toggleResult = overlay.dispatchPreviewToggle();
  EXPECT(toggleResult.ok, "overlay preview dispatch succeeds");
  EXPECT(fixture.editorState.isPreviewEnabled(),
         "overlay dispatch reuses preview command path");
  EXPECT(!overlay.shouldRenderEditorOverlay(),
         "preview on hides editor overlay state");
  EXPECT(fixture.bus.history().back().line == "preview toggle",
         "overlay dispatch records command bus line");
}

void testViewportOverlayEnqueueDebugDrawTracksPreviewVisibility() {
  Fixture fixture;
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();
  overlay.enqueueDebugDraw();
  LX_core::DebugDraw::endFrame();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() > 12,
         "overlay debug draw should emit selection/frustum/light lines when "
         "preview is off");

  const auto previewResult = overlay.dispatchPreviewToggle();
  EXPECT(previewResult.ok,
         "preview toggle should succeed before hidden debug draw check");
  LX_core::DebugDraw::beginFrame();
  overlay.enqueueDebugDraw();
  LX_core::DebugDraw::endFrame();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() == 0,
         "overlay debug draw should be hidden while preview is on");
}

void testViewportOverlayBoxSelectionReplaceSelectsIntersectingMeshes() {
  Fixture fixture;
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  const auto result = overlay.dispatchBoxSelection(
      {100.0f, 100.0f}, {700.0f, 500.0f}, {800.0f, 600.0f}, false, false);
  EXPECT(result.ok, "box selection replace should succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 3,
         "box selection should select all three projected meshes");
  EXPECT(selected[0] == fixture.cube && selected[1] == fixture.sphere &&
             selected[2] == fixture.cone,
         "box selection should preserve projected node order");
}

void testViewportOverlayBoxSelectionCtrlAppendKeepsExistingSelection() {
  Fixture fixture;
  EXPECT(fixture.bus.dispatch("select /world/cube").ok,
         "initial select should succeed");
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  const auto result = overlay.dispatchBoxSelection(
      {300.0f, 180.0f}, {520.0f, 420.0f}, {800.0f, 600.0f}, true, false);
  EXPECT(result.ok, "ctrl box selection append should succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 2, "ctrl box selection should append hit node");
  EXPECT(selected[0] == fixture.cube && selected[1] == fixture.sphere,
         "ctrl box selection should preserve previous selection then append "
         "new hit");
  EXPECT(fixture.editorState.getPrimarySelected().has_value() &&
             &fixture.editorState.getPrimarySelected()->get() ==
                 fixture.sphere.get(),
         "ctrl box selection should make appended node primary");
}

void testViewportOverlayLargeSelectionRequiresConfirmation() {
  Fixture fixture;
  EXPECT(fixture.bus.dispatch("select /world/cube").ok,
         "initial select should succeed");
  LX_core::EditorConfig config;
  config.boxSelectConfirmThreshold = 0.2f;
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene, config);

  const auto pending = overlay.dispatchBoxSelection(
      {0.0f, 0.0f}, {799.0f, 599.0f}, {800.0f, 600.0f}, false, false);
  EXPECT(pending.ok,
         "large box selection should produce pending confirmation state");
  EXPECT(overlay.hasPendingBoxSelectionConfirmation(),
         "large selection should require confirmation");
  EXPECT(fixture.editorState.getSelected().size() == 1 &&
             fixture.editorState.getSelected()[0] == fixture.cube,
         "pending confirmation should not mutate selection before resolve");

  const auto cancel = overlay.resolvePendingBoxSelection(false);
  EXPECT(cancel.ok, "cancel pending selection should succeed");
  EXPECT(!overlay.hasPendingBoxSelectionConfirmation(),
         "cancel should clear confirmation state");
  EXPECT(fixture.editorState.getSelected().size() == 1 &&
             fixture.editorState.getSelected()[0] == fixture.cube,
         "cancel should preserve previous selection");
}

void testViewportOverlayGizmoModeHotkeysAndCommitPath() {
  Fixture fixture;
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  EXPECT(overlay.getGizmoOperation() ==
             LX_core::ViewportOverlay::GizmoOperation::Translate,
         "default gizmo mode is translate");
  EXPECT(overlay.handleGizmoHotkeys('E'), "E switches gizmo mode");
  EXPECT(overlay.getGizmoOperation() ==
             LX_core::ViewportOverlay::GizmoOperation::Rotate,
         "E selects rotate");
  EXPECT(overlay.handleGizmoHotkeys('R'), "R switches gizmo mode");
  EXPECT(overlay.getGizmoOperation() ==
             LX_core::ViewportOverlay::GizmoOperation::Scale,
         "R selects scale");
  EXPECT(overlay.handleGizmoHotkeys('W'), "W switches gizmo mode");
  EXPECT(overlay.getGizmoOperation() ==
             LX_core::ViewportOverlay::GizmoOperation::Translate,
         "W selects translate");

  LX_core::GizmoTransformComponents components;
  components.translation = {7.0f, 8.0f, 9.0f};
  const auto moveResult =
      overlay.dispatchGizmoCommit("/world/cube", components);
  EXPECT(moveResult.ok, "translate commit dispatches move command");
  EXPECT(fixture.bus.history().back().line.find("move") == 0,
         "translate commit records move command");
}

void testViewportOverlayRotateCommitDrivesDirectionalLight() {
  Fixture fixture;
  auto lightNode = LX_core::SceneNode::create("key_light_node");
  lightNode->setName("key_light");
  fixture.scene->addRenderable(lightNode);
  auto light = std::make_shared<LX_core::DirectionalLight>();
  fixture.scene->attachLight(lightNode, light);

  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);
  overlay.setGizmoOperation(LX_core::ViewportOverlay::GizmoOperation::Rotate);

  LX_core::GizmoTransformComponents components;
  components.rotationEulerDegrees = {0.0f, 90.0f, 0.0f};
  const auto result = overlay.dispatchGizmoCommit("/key_light", components);
  EXPECT(result.ok, "rotate gizmo commit succeeds for light");
  (void)fixture.scene->getSceneLevelResources(LX_core::Pass_Forward,
                                              LX_core::RenderTarget{});
  const LX_core::Vec4f dir =
      fixture.scene->getSceneLightsUBO()->param.directional[0].direction;
  EXPECT(std::abs(dir.x + 1.0f) <= 1e-4f && std::abs(dir.y) <= 1e-4f &&
             std::abs(dir.z) <= 1e-4f,
         "gizmo rotate commit updates directional light direction");
}

void testViewportOverlayMultiSelectionCommitAppliesSharedDelta() {
  Fixture fixture;
  fixture.cube->setTranslation({2.0f, 0.0f, 0.0f});
  fixture.sphere->setTranslation({5.0f, 1.0f, 0.0f});
  fixture.editorState.select({fixture.cube, fixture.sphere});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  std::vector<LX_core::Transform> before = {
      fixture.cube->getLocalTransform(), fixture.sphere->getLocalTransform()};
  std::vector<LX_core::Transform> after = before;
  after[0].translation = {3.0f, 2.0f, 0.0f};
  after[1].translation = {6.0f, 3.0f, 0.0f};

  const auto commit = overlay.dispatchGizmoSelectionCommit(
      {"/world/cube", "/world/sphere"}, before, after);
  EXPECT(commit.ok, "multi-selection gizmo commit should succeed");
  EXPECT(fixture.bus.history().back().line ==
             "move \"/world/cube\" \"/world/sphere\" 1 2 0",
         "multi-selection translate should commit one multi-target move delta");

  const auto undo = fixture.bus.dispatch("undo");
  EXPECT(undo.ok, "undo after gizmo selection commit should succeed");
  EXPECT(fixture.cube->getTranslation().x == 2.0f &&
             fixture.sphere->getTranslation().x == 5.0f,
         "undo should restore both selected node transforms");
}

void testViewportOverlayDrawSmoke() {
  if (!setupMinimalImGui()) {
    std::cout
        << "[SKIP] viewport_overlay draw smoke (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);

  try {
    ImGui::NewFrame();
    overlay.drawSceneOverlay(LX_demo::lxe_editor::SceneViewRect{
        .x = 0.0f, .y = 0.0f, .width = 1280.0f, .height = 720.0f});
    const auto panelRect = overlay.getPanelRect();
    ImGuiWindow *viewportWindow = ImGui::FindWindowByName("Viewport");
    EXPECT(viewportWindow == nullptr,
           "viewport overlay should not create a Viewport window");
    EXPECT(panelRect.size.x > 0.0f && panelRect.size.y > 0.0f,
           "viewport draw should record a non-empty panel rect");
    EXPECT(panelRect.origin.x == 0.0f && panelRect.origin.y == 0.0f &&
               panelRect.size.x == 1280.0f && panelRect.size.y == 720.0f,
           "panel rect should mirror the supplied scene rect");
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "ViewportOverlay scene overlay draw should not throw in "
                  "CPU-only ImGui frame");
  }

  ImGui::DestroyContext();
}

void testViewportOverlayClearsCaptureStateOnInvalidSceneRect() {
  Fixture fixture;
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);
  overlay.m_gizmoHovered = true;
  overlay.m_gizmoUsing = true;
  overlay.m_gizmoDragPaths.push_back("/world/cube");
  overlay.m_gizmoPreDragTransforms.push_back(fixture.cube->getLocalTransform());

  overlay.drawSceneOverlay(LX_demo::lxe_editor::SceneViewRect{
      .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f});

  EXPECT(!overlay.isGizmoCapturingMouse(),
         "invalid scene rect should clear stale gizmo capture state");
  EXPECT(overlay.m_gizmoDragPaths.empty() &&
             overlay.m_gizmoPreDragTransforms.empty(),
         "invalid scene rect should clear stale gizmo drag bookkeeping");
}

void testViewportOverlayClearsCaptureStateWithoutImGuiContext() {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }

  Fixture fixture;
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);
  overlay.m_gizmoHovered = true;
  overlay.m_gizmoUsing = true;
  overlay.m_gizmoDragPaths.push_back("/world/cube");
  overlay.m_gizmoPreDragTransforms.push_back(fixture.cube->getLocalTransform());

  overlay.drawSceneOverlay(LX_demo::lxe_editor::SceneViewRect{
      .x = 0.0f, .y = 0.0f, .width = 1280.0f, .height = 720.0f});

  EXPECT(!overlay.isGizmoCapturingMouse(),
         "missing ImGui context should clear stale gizmo capture state");
  EXPECT(overlay.m_gizmoDragPaths.empty() &&
             overlay.m_gizmoPreDragTransforms.empty(),
         "missing ImGui context should clear stale gizmo drag bookkeeping");
}

void testViewportOverlayClearsCaptureStateWhenEditorCameraIsMissing() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] viewport_overlay stale capture test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  fixture.editorState.setEditorCamera(LX_core::SceneNodeSharedPtr{});
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);
  overlay.m_gizmoHovered = true;
  overlay.m_gizmoUsing = true;
  overlay.m_gizmoDragPaths.push_back("/world/cube");
  overlay.m_gizmoPreDragTransforms.push_back(fixture.cube->getLocalTransform());

  ImGui::NewFrame();
  overlay.drawSceneOverlay(LX_demo::lxe_editor::SceneViewRect{
      .x = 0.0f, .y = 0.0f, .width = 1280.0f, .height = 720.0f});

  EXPECT(!overlay.isGizmoCapturingMouse(),
         "missing editor camera should clear stale gizmo capture state");
  EXPECT(overlay.m_gizmoDragPaths.empty() &&
             overlay.m_gizmoPreDragTransforms.empty(),
         "missing editor camera should clear stale gizmo drag bookkeeping");

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

void testViewportOverlayClearsCaptureStateWhenEditorCameraHasNoComponent() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] viewport_overlay camera component test (font atlas "
                 "unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  auto cameraNodeWithoutComponent = LX_core::SceneNode::create("camera_shell");
  fixture.editorState.setEditorCamera(cameraNodeWithoutComponent);
  fixture.editorState.select({fixture.cube});
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);
  overlay.m_gizmoHovered = true;
  overlay.m_gizmoUsing = true;
  overlay.m_gizmoDragPaths.push_back("/world/cube");
  overlay.m_gizmoPreDragTransforms.push_back(fixture.cube->getLocalTransform());

  ImGui::NewFrame();
  overlay.drawSceneOverlay(LX_demo::lxe_editor::SceneViewRect{
      .x = 0.0f, .y = 0.0f, .width = 1280.0f, .height = 720.0f});

  EXPECT(!overlay.isGizmoCapturingMouse(),
         "editor camera without CameraComponent should clear stale gizmo "
         "capture state");
  EXPECT(overlay.m_gizmoDragPaths.empty() &&
             overlay.m_gizmoPreDragTransforms.empty(),
         "editor camera without CameraComponent should clear stale gizmo drag "
         "bookkeeping");

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

} // namespace

int main() {
  expSetEnvVK();
  testEditorStateSyncsActiveCameraAcrossPreviewToggle();
  testViewportOverlaySnapshotAndCommandEntry();
  testViewportOverlayGizmoModeHotkeysAndCommitPath();
  testViewportOverlayRotateCommitDrivesDirectionalLight();
  testViewportOverlayMultiSelectionCommitAppliesSharedDelta();
  testViewportOverlayEnqueueDebugDrawTracksPreviewVisibility();
  testViewportOverlayBoxSelectionReplaceSelectsIntersectingMeshes();
  testViewportOverlayBoxSelectionCtrlAppendKeepsExistingSelection();
  testViewportOverlayLargeSelectionRequiresConfirmation();
  testViewportOverlayDrawSmoke();
  testViewportOverlayClearsCaptureStateOnInvalidSceneRect();
  testViewportOverlayClearsCaptureStateWithoutImGuiContext();
  testViewportOverlayClearsCaptureStateWhenEditorCameraIsMissing();
  testViewportOverlayClearsCaptureStateWhenEditorCameraHasNoComponent();

  if (failures == 0) {
    std::cout << "[PASS] viewport_overlay tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
