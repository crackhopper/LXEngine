#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include <imgui.h>

#include <cmath>
#include <iostream>

namespace {

constexpr float kEps = 1e-4f;
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] bool nearlyEqual(const float a, const float b) {
  return std::fabs(a - b) <= kEps;
}

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

struct Fixture {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::SceneNodeSharedPtr world = LX_core::SceneNode::create("world");
  LX_core::SceneNodeSharedPtr cube = LX_core::SceneNode::create("cube");
  LX_core::SceneNodeSharedPtr cameraNode = LX_core::SceneNode::create("cam_node");
  LX_core::SceneNodeSharedPtr lightNode = LX_core::SceneNode::create("light_node");
  LX_core::SceneSharedPtr scene;

  Fixture() {
    world->setName("world");
    cube->setName("cube");
    cube->setParent(world);
    cube->setTranslation({1.0f, 2.0f, 3.0f});
    cube->setVisibilityLayerMask(0x12345678u);

    cameraNode->setName("game_cam");
    auto camera = cameraNode->addComponent<LX_core::CameraComponent>();
    camera->get().fovY = 70.0f;
    camera->get().nearPlane = 0.5f;
    camera->get().farPlane = 250.0f;
    camera->get().type = LX_core::CameraType::Orthographic;
    camera->get().setCullingMask(0x00FF00FFu);

    lightNode->setName("dir_light");

    scene = LX_core::Scene::create("inspector_panel", world);
    scene->addRenderable(cube);
    scene->addRenderable(lightNode);
    scene->addCamera(cameraNode);
    auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
        scene->getLights().front());
    scene->attachLight(lightNode, dirLight);
    dirLight->ubo->param.dir = LX_core::Vec4f{-0.3f, -1.0f, -0.5f, 0.0f};
    dirLight->ubo->param.color = LX_core::Vec4f{0.9f, 0.8f, 0.7f, 2.5f};
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testSnapshotWithoutSelection() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);

  const auto snapshot = panel.makeSnapshot();
  EXPECT(!snapshot.hasSelection, "snapshot should report no selection by default");
}

void testSnapshotForRegularNode() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cube});

  const auto snapshot = panel.makeSnapshot();
  EXPECT(snapshot.hasSelection, "regular node snapshot should have selection");
  EXPECT(snapshot.path == "/world/cube", "regular node path should match");
  EXPECT(snapshot.name == "cube", "regular node name should match");
  EXPECT(nearlyEqual(snapshot.translation.x, 1.0f) &&
             nearlyEqual(snapshot.translation.y, 2.0f) &&
             nearlyEqual(snapshot.translation.z, 3.0f),
         "regular node translation should be reported");
  EXPECT(snapshot.visibilityMask == 0x12345678u,
         "regular node visibility mask should be reported");
  EXPECT(!snapshot.hasCamera, "regular node should not report camera component");
}

void testSnapshotForCameraNode() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cameraNode});

  const auto snapshot = panel.makeSnapshot();
  EXPECT(snapshot.hasSelection, "camera node snapshot should have selection");
  EXPECT(snapshot.hasCamera, "camera node should report camera component");
  EXPECT(snapshot.path == "/game_cam", "camera node path should match");
  EXPECT(nearlyEqual(snapshot.cameraFov, 70.0f), "camera fov should match");
  EXPECT(nearlyEqual(snapshot.cameraNear, 0.5f), "camera near should match");
  EXPECT(nearlyEqual(snapshot.cameraFar, 250.0f), "camera far should match");
  EXPECT(!snapshot.cameraPerspective, "camera projection should match");
  EXPECT(snapshot.cameraCullingMask == 0x00FF00FFu,
         "camera culling mask should match");
}

void testSnapshotForLightNode() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.lightNode});

  const auto snapshot = panel.makeSnapshot();
  EXPECT(snapshot.hasSelection, "light node snapshot should have selection");
  EXPECT(snapshot.hasLight, "light node should report directional light fields");
  EXPECT(snapshot.path == "/dir_light", "light node path should match");
  EXPECT(nearlyEqual(snapshot.lightDirection.x, -0.3f) &&
             nearlyEqual(snapshot.lightDirection.y, -1.0f) &&
             nearlyEqual(snapshot.lightDirection.z, -0.5f),
         "light direction should match scene light");
  EXPECT(nearlyEqual(snapshot.lightColor.x, 0.9f) &&
             nearlyEqual(snapshot.lightColor.y, 0.8f) &&
             nearlyEqual(snapshot.lightColor.z, 0.7f),
         "light color should match scene light");
  EXPECT(nearlyEqual(snapshot.lightIntensity, 2.5f),
         "light intensity should match scene light");
}

void testSnapshotForRenamedLightNodeUsesExactAttachedLight() {
  Fixture fixture;
  fixture.lightNode->setName("sun");

  auto fillNode = LX_core::SceneNode::create("fill_light_node");
  fillNode->setName("fill");
  fixture.scene->addRenderable(fillNode);
  auto fillLight = std::make_shared<LX_core::DirectionalLight>();
  fillLight->ubo->param.dir = LX_core::Vec4f{1.0f, 0.0f, 0.0f, 0.0f};
  fillLight->ubo->param.color = LX_core::Vec4f{0.1f, 0.2f, 0.3f, 9.0f};
  fixture.scene->attachLight(fillNode, fillLight);

  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.lightNode});

  const auto snapshot = panel.makeSnapshot();
  EXPECT(snapshot.hasLight, "renamed light node should still report light fields");
  EXPECT(snapshot.path == "/sun", "renamed light node path should match");
  EXPECT(nearlyEqual(snapshot.lightDirection.x, -0.3f) &&
             nearlyEqual(snapshot.lightDirection.y, -1.0f) &&
             nearlyEqual(snapshot.lightDirection.z, -0.5f),
         "renamed light snapshot should keep the exact attached light direction");
  EXPECT(nearlyEqual(snapshot.lightIntensity, 2.5f),
         "renamed light snapshot should keep the exact attached light intensity");
}

void testSnapshotTracksExternalNodeMutationAfterSelection() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cube});

  const auto before = panel.makeSnapshot();
  EXPECT(nearlyEqual(before.translation.x, 1.0f) &&
             nearlyEqual(before.translation.y, 2.0f) &&
             nearlyEqual(before.translation.z, 3.0f),
         "precondition: initial snapshot should expose original translation");

  fixture.cube->setTranslation({9.0f, 8.0f, 7.0f});

  const auto after = panel.makeSnapshot();
  EXPECT(nearlyEqual(after.translation.x, 9.0f) &&
             nearlyEqual(after.translation.y, 8.0f) &&
             nearlyEqual(after.translation.z, 7.0f),
         "snapshot should reflect external runtime mutation for selected node");
}

void testDispatchHelpersUseCommandBus() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);

  const auto rename = panel.dispatchRename("/world/cube", "hero_cube");
  EXPECT(rename.ok, "rename helper should succeed");
  EXPECT(fixture.cube->getName() == "hero_cube", "rename helper should update node name");
  EXPECT(!fixture.bus.history().empty() &&
             fixture.bus.history().back().line == "set \"/world/cube.name\" \"hero_cube\"",
         "rename helper should record quoted set command");

  const auto move = panel.dispatchMove("/world/hero_cube", {4.0f, 5.0f, 6.0f});
  EXPECT(move.ok, "move helper should succeed after rename");
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, 4.0f) &&
             nearlyEqual(fixture.cube->getTranslation().y, 5.0f) &&
             nearlyEqual(fixture.cube->getTranslation().z, 6.0f),
         "move helper should update translation through builtin command");
  EXPECT(fixture.bus.history().back().line ==
             "set \"/world/hero_cube.translation\" 4.000 5.000 6.000",
         "move helper should unify through set command line");

  const auto setVisibility =
      panel.dispatchSetUnsigned("/world/hero_cube", "visibilityMask", 255u);
  EXPECT(setVisibility.ok, "visibility helper should succeed");
  EXPECT(fixture.cube->getVisibilityLayerMask() == 255u,
         "visibility helper should update node visibility mask");

  const auto setDirection =
      panel.dispatchSetVec3("/dir_light", "direction", {0.0f, -1.0f, 0.0f});
  EXPECT(setDirection.ok, "light direction helper should succeed");
  const auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
      fixture.scene->getLights().front());
  EXPECT(nearlyEqual(dirLight->ubo->param.dir.x, 0.0f) &&
             nearlyEqual(dirLight->ubo->param.dir.y, -1.0f) &&
             nearlyEqual(dirLight->ubo->param.dir.z, 0.0f),
         "light direction helper should update scene light");
}

void testDrawFrameSurvivesCpuOnlyImGui() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] inspector_panel draw smoke (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cube});

  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "InspectorPanel draw should not throw in CPU-only ImGui frame");
  }

  ImGui::DestroyContext();
}

void testDrawResyncsInspectorDraftAfterExternalMutation() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] inspector stale-draft draw regression\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cube});

  ImGui::NewFrame();
  panel.draw();
  ImGui::EndFrame();

  fixture.cube->setTranslation({4.0f, 5.0f, 6.0f});

  const auto runtimeSnapshotBeforeResync = panel.makeSnapshot();
  EXPECT(nearlyEqual(runtimeSnapshotBeforeResync.translation.x, 4.0f) &&
             nearlyEqual(runtimeSnapshotBeforeResync.translation.y, 5.0f) &&
             nearlyEqual(runtimeSnapshotBeforeResync.translation.z, 6.0f),
         "selected-node snapshot should observe the external mutation immediately");

  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "draw should survive external mutation-triggered resync");
  }

  const auto snapshotAfterDraw = panel.makeSnapshot();
  EXPECT(snapshotAfterDraw.path == "/world/cube",
         "draw after mutation should preserve the active selected path");
  EXPECT(nearlyEqual(snapshotAfterDraw.translation.x, 4.0f) &&
             nearlyEqual(snapshotAfterDraw.translation.y, 5.0f) &&
             nearlyEqual(snapshotAfterDraw.translation.z, 6.0f),
         "draw after mutation should leave the inspector snapshot on the latest transform");

  ImGui::DestroyContext();
}

void testSceneSubscriptionSwitchIgnoresOldSceneMutations() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] inspector scene-switch subscription regression\n";
    ImGui::DestroyContext();
    return;
  }

  LX_core::EditorState editorState;
  LX_core::CommandBus bus;

  auto rootA = LX_core::SceneNode::create("root_a");
  rootA->setName("rootA");
  auto nodeA = LX_core::SceneNode::create("node_a");
  nodeA->setName("nodeA");
  nodeA->setParent(rootA);
  nodeA->setTranslation({1.0f, 2.0f, 3.0f});
  auto sceneA = LX_core::Scene::create("scene_a", rootA);
  sceneA->addRenderable(nodeA);

  auto rootB = LX_core::SceneNode::create("root_b");
  rootB->setName("rootB");
  auto nodeB = LX_core::SceneNode::create("node_b");
  nodeB->setName("nodeB");
  nodeB->setParent(rootB);
  nodeB->setTranslation({7.0f, 8.0f, 9.0f});
  auto sceneB = LX_core::Scene::create("scene_b", rootB);
  sceneB->addRenderable(nodeB);

  LX_core::registerBuiltinCommands(bus, editorState, *sceneA);
  LX_core::registerBuiltinCommands(bus, editorState, *sceneB);

  LX_core::InspectorPanel panel(bus, editorState);
  editorState.select({nodeA});

  ImGui::NewFrame();
  panel.draw();
  ImGui::EndFrame();

  const auto sceneASnapshot = panel.makeSnapshot();
  EXPECT(sceneASnapshot.path == "/rootA/nodeA",
         "initial draw should reflect the first scene selection");
  EXPECT(nearlyEqual(sceneASnapshot.translation.x, 1.0f) &&
             nearlyEqual(sceneASnapshot.translation.y, 2.0f) &&
             nearlyEqual(sceneASnapshot.translation.z, 3.0f),
         "initial snapshot should match the first scene selection");

  editorState.select({nodeB});
  ImGui::NewFrame();
  panel.draw();
  ImGui::EndFrame();

  const auto sceneBSnapshot = panel.makeSnapshot();
  EXPECT(sceneBSnapshot.path == "/rootB/nodeB",
         "draw after selection switch should reflect the new scene selection");
  EXPECT(nearlyEqual(sceneBSnapshot.translation.x, 7.0f) &&
             nearlyEqual(sceneBSnapshot.translation.y, 8.0f) &&
             nearlyEqual(sceneBSnapshot.translation.z, 9.0f),
         "snapshot after selection switch should match the new scene selection");

  nodeA->setTranslation({11.0f, 12.0f, 13.0f});

  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false,
           "draw should survive mutations from a scene that is no longer selected");
  }

  const auto postSceneAMutationSnapshot = panel.makeSnapshot();
  EXPECT(postSceneAMutationSnapshot.path == "/rootB/nodeB",
         "current snapshot should still follow the active selection after old-scene mutation");
  EXPECT(nearlyEqual(postSceneAMutationSnapshot.translation.x, 7.0f) &&
             nearlyEqual(postSceneAMutationSnapshot.translation.y, 8.0f) &&
             nearlyEqual(postSceneAMutationSnapshot.translation.z, 9.0f),
         "old-scene mutation should not change the selected node snapshot");

  editorState.deselect();
  nodeB->setTranslation({14.0f, 15.0f, 16.0f});
  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false,
           "draw should survive mutations after the inspector selection is cleared");
  }

  const auto noSelectionSnapshot = panel.makeSnapshot();
  EXPECT(!noSelectionSnapshot.hasSelection,
         "deselected panel should stay empty even when the prior scene mutates");

  ImGui::DestroyContext();
}

} // namespace

int main() {
  expSetEnvVK();
  testSnapshotWithoutSelection();
  testSnapshotForRegularNode();
  testSnapshotForCameraNode();
  testSnapshotForLightNode();
  testSnapshotForRenamedLightNodeUsesExactAttachedLight();
  testSnapshotTracksExternalNodeMutationAfterSelection();
  testDispatchHelpersUseCommandBus();
  testDrawFrameSurvivesCpuOnlyImGui();
  testDrawResyncsInspectorDraftAfterExternalMutation();
  testSceneSubscriptionSwitchIgnoresOldSceneMutations();

  if (failures == 0) {
    std::cout << "[PASS] inspector_panel tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
