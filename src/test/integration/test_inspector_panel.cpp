#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/scene/components/camera_component.hpp"
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
  LX_core::SceneSharedPtr scene;

  Fixture() {
    world->setName("world");
    cube->setName("cube");
    cube->setParent(world);
    cube->setTranslation({1.0f, 2.0f, 3.0f});

    cameraNode->setName("game_cam");
    auto camera = cameraNode->addComponent<LX_core::CameraComponent>();
    camera->get().fovY = 70.0f;

    scene = LX_core::Scene::create("inspector_panel", world);
    scene->addRenderable(cube);
    scene->addCamera(cameraNode);
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
  fixture.editorState.select(fixture.cube);

  const auto snapshot = panel.makeSnapshot();
  EXPECT(snapshot.hasSelection, "regular node snapshot should have selection");
  EXPECT(snapshot.path == "/world/cube", "regular node path should match");
  EXPECT(snapshot.name == "cube", "regular node name should match");
  EXPECT(nearlyEqual(snapshot.translation.x, 1.0f) &&
             nearlyEqual(snapshot.translation.y, 2.0f) &&
             nearlyEqual(snapshot.translation.z, 3.0f),
         "regular node translation should be reported");
  EXPECT(!snapshot.hasCamera, "regular node should not report camera component");
}

void testSnapshotForCameraNode() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select(fixture.cameraNode);

  const auto snapshot = panel.makeSnapshot();
  EXPECT(snapshot.hasSelection, "camera node snapshot should have selection");
  EXPECT(snapshot.hasCamera, "camera node should report camera component");
  EXPECT(snapshot.path == "/game_cam", "camera node path should match");
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
  EXPECT(fixture.bus.history().back().line == "move \"/world/hero_cube\" 4.000 5.000 6.000",
         "move helper should record move command line");
}

void testDrawFrameSurvivesCpuOnlyImGui() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] inspector_panel draw smoke (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select(fixture.cube);

  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "InspectorPanel draw should not throw in CPU-only ImGui frame");
  }

  ImGui::DestroyContext();
}

} // namespace

int main() {
  expSetEnvVK();
  testSnapshotWithoutSelection();
  testSnapshotForRegularNode();
  testSnapshotForCameraNode();
  testDispatchHelpersUseCommandBus();
  testDrawFrameSurvivesCpuOnlyImGui();

  if (failures == 0) {
    std::cout << "[PASS] inspector_panel tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
