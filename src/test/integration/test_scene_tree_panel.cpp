#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include <imgui.h>

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

struct Fixture {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::SceneNodeSharedPtr world = LX_core::SceneNode::create("world");
  LX_core::SceneNodeSharedPtr cube = LX_core::SceneNode::create("cube");
  LX_core::SceneNodeSharedPtr light = LX_core::SceneNode::create("light");
  LX_core::SceneSharedPtr scene;

  Fixture() {
    world->setName("world");
    cube->setName("cube");
    light->setName("sun");
    cube->setParent(world);
    light->setParent(world);

    scene = LX_core::Scene::create("scene_tree_panel", world);
    scene->addRenderable(cube);
    scene->addRenderable(light);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testPathJumpDispatchesSelect() {
  Fixture fixture;
  LX_core::SceneTreePanel panel(fixture.bus, fixture.editorState, *fixture.scene);

  panel.setPathInputText("/world/cube");
  const LX_core::CommandResult result = panel.submitPathJump();

  EXPECT(result.ok, "path jump should succeed for existing node");
  EXPECT(fixture.editorState.getSelected() == fixture.cube,
         "path jump should select cube");
  EXPECT(!fixture.bus.history().empty(), "path jump should dispatch command");
  EXPECT(fixture.bus.history().back().line == "select /world/cube",
         "path jump should dispatch select command line");
}

void testDispatchSelectPathUsesCommandBus() {
  Fixture fixture;
  LX_core::SceneTreePanel panel(fixture.bus, fixture.editorState, *fixture.scene);

  const LX_core::CommandResult result = panel.dispatchSelectPath("/world/sun");

  EXPECT(result.ok, "dispatchSelectPath should succeed");
  EXPECT(fixture.editorState.getSelected() == fixture.light,
         "dispatchSelectPath should update selected node via builtin command");
  EXPECT(fixture.bus.history().back().line == "select /world/sun",
         "dispatchSelectPath should go through command bus history");
}

void testDrawFrameSurvivesCpuOnlyImGui() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_tree_panel draw smoke (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  LX_core::SceneTreePanel panel(fixture.bus, fixture.editorState, *fixture.scene);
  panel.setPathInputText("/world");

  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "SceneTreePanel draw should not throw in CPU-only ImGui frame");
  }

  ImGui::DestroyContext();
}

} // namespace

int main() {
  expSetEnvVK();
  testPathJumpDispatchesSelect();
  testDispatchSelectPathUsesCommandBus();
  testDrawFrameSurvivesCpuOnlyImGui();

  if (failures == 0) {
    std::cout << "[PASS] scene_tree_panel tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
