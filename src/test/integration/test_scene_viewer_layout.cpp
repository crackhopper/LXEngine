#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "demos/scene_viewer/camera_rig.hpp"
#include "demos/scene_viewer/ui_overlay.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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

void testDefaultLayoutPlacesViewportBetweenPanels() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] scene_viewer layout test (font atlas unavailable)\n";
    ImGui::DestroyContext();
    return;
  }

  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  auto scene = LX_core::Scene::create("layout_scene");
  auto editorCameraNode = LX_core::SceneNode::create("editor_camera");
  editorCameraNode->setName("editor_cam");
  auto editorCamera = editorCameraNode->addComponent<LX_core::CameraComponent>();
  scene->addCamera(editorCameraNode);
  editorState.setEditorCamera(editorCameraNode);
  editorState.setPreviewCamera(editorCameraNode);
  (void)editorState.syncActiveCamera(*scene);
  LX_core::registerBuiltinCommands(bus, editorState, *scene);

  LX_demo::scene_viewer::CameraRig rig;
  rig.attach(editorCamera->get());
  LX_core::SceneTreePanel sceneTreePanel(bus, editorState, *scene);
  LX_core::InspectorPanel inspectorPanel(bus, editorState);
  LX_core::ConsolePanel consolePanel(bus);
  LX_core::ViewportOverlay viewportOverlay(bus, editorState, *scene);
  LX_demo::scene_viewer::UiOverlay ui;
  ui.attach(rig, bus, sceneTreePanel, inspectorPanel, consolePanel,
            viewportOverlay);

  ImGui::NewFrame();
  ui.drawFrame();

  ImGuiWindow *sceneTree = ImGui::FindWindowByName("Scene Tree");
  ImGuiWindow *inspector = ImGui::FindWindowByName("Inspector");
  ImGuiWindow *console = ImGui::FindWindowByName("Command Console");
  ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");

  EXPECT(sceneTree != nullptr, "scene tree window should exist");
  EXPECT(inspector != nullptr, "inspector window should exist");
  EXPECT(console != nullptr, "console window should exist");
  EXPECT(viewport != nullptr, "viewport window should exist");

  if (sceneTree && inspector && console && viewport) {
    EXPECT(sceneTree->Pos.x < viewport->Pos.x,
           "scene tree should be left of viewport");
    EXPECT(inspector->Pos.x > viewport->Pos.x,
           "inspector should be right of viewport");
    EXPECT(console->Pos.y >= viewport->Pos.y + viewport->Size.y - 1.0f,
           "console should be below viewport");
    EXPECT(viewportOverlay.getPanelRect().size.x > 0.0f &&
               viewportOverlay.getPanelRect().size.y > 0.0f,
           "viewport overlay should bind to viewport content rect");
  }

  ImGui::EndFrame();
  ImGui::DestroyContext();
}

} // namespace

int main() {
  expSetEnvVK();
  testDefaultLayoutPlacesViewportBetweenPanels();

  if (failures == 0) {
    std::cout << "[PASS] scene_viewer layout tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
