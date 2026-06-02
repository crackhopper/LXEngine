#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/lxe_editor_commands.hpp"
#include "demos/lxe_editor/realtime_render_profile.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/scene_view_rect.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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

void testRealtimeRenderLsAndRun() {
  LX_core::CommandBus bus;
  LX_core::EditorState editorState;
  LX_core::SceneSharedPtr scene = LX_core::Scene::create(nullptr);
  LX_demo::lxe_editor::SceneInteractionController interaction{bus, editorState,
                                                              *scene};
  LX_demo::lxe_editor::LxeEditorCommandContext context{
      .editorState = editorState,
      .scene = *scene,
      .interaction = interaction,
      .getEditMode = []() { return 0; },
      .setEditMode = [](int) {},
      .getCameraControlMode = []() { return 0; },
      .setCameraControlMode = [](int) {},
      .sceneViewRect = []() { return LX_demo::lxe_editor::SceneViewRect{}; },
      .dirty = []() { return false; },
      .debugEnabled = []() { return false; },
      .setDebugEnabled = [](bool) {},
      .runtimeScenePath = []() { return std::optional<std::string>{}; },
      .projectSummaryJson = []() { return std::string("{}"); },
      .persistedHistory = []() { return std::vector<std::string>{}; },
      .appendConsoleDebugLine = [](std::string_view) {},
      .realtimeRenderListJson =
          []() {
            return std::string(
                "{\"profiles\":[{\"name\":\"preview\",\"camera\":\"/"
                "game_cam\",\"width\":64,\"height\":36,"
                "\"outputFormat\":\"exr-png\",\"outDir\":\"artifacts\"}]}");
          },
      .realtimeRenderRun =
          [](std::string_view profile) -> LX_core::CommandResult {
        if (profile != "preview") {
          return LX_core::CommandResult{false, "bad profile", {}, {}};
        }
        return LX_core::CommandResult{
            true,
            "realtime profile generated",
            "{\"linearExr\":\"/tmp/linear.exr\","
            "\"cpuSrgbPng\":\"/tmp/cpu.png\","
            "\"pipelineSrgbPng\":\"/tmp/pipeline.png\"}",
            {}};
      },
  };

  LX_demo::lxe_editor::registerLxeEditorCommands(bus, context);

  const auto list = bus.dispatch("realtime-render ls");
  EXPECT(list.ok, "ls should succeed");
  EXPECT(list.structured.find("\"profiles\"") != std::string::npos,
         "ls returns json");

  const auto run = bus.dispatch("realtime-render run preview");
  EXPECT(run.ok, "run should succeed");
  EXPECT(run.structured.find("linear.exr") != std::string::npos,
         "run returns output paths");
}

void testRealtimeProfileOutputHelpersBuildStableJson() {
  LX_core::offline::OutputProfile output;
  output.width = 320;
  output.height = 180;
  output.outDir = "artifacts/offline/preview";

  const std::filesystem::path base =
      LX_demo::lxe_editor::makeRealtimeProfileOutputBasePath(
          "Scene/One", "preview:fast", output);
  EXPECT(base.generic_string() ==
             "artifacts/offline/preview/realtime/Scene%2FOne/"
             "preview%3Afast/render",
         "base path should include encoded realtime scene and profile "
         "components");
  const std::filesystem::path distinctBase =
      LX_demo::lxe_editor::makeRealtimeProfileOutputBasePath(
          "Scene/One", "preview/fast", output);
  EXPECT(distinctBase != base,
         "encoded profile path components should avoid sanitizer collisions");

  const std::string json =
      LX_demo::lxe_editor::realtimeProfileOutputResultJson(
          "preview:fast",
          LX_demo::lxe_editor::RealtimeProfileOutputResult{
              .linearExrPath = base.parent_path() / "linear.exr",
              .cpuSrgbPngPath = base.parent_path() / "cpu.png",
              .pipelineSrgbPngPath = base.parent_path() / "pipeline.png",
              .metadataPath = base.parent_path() / "render.json",
              .width = output.width,
              .height = output.height,
          });

  EXPECT(json.find("\"profile\":\"preview:fast\"") != std::string::npos,
         "result JSON should include escaped profile name");
  EXPECT(json.find("\"width\":320") != std::string::npos,
         "result JSON should include width");
  EXPECT(json.find("\"height\":180") != std::string::npos,
         "result JSON should include height");
  EXPECT(json.find("\"linearExrPath\"") != std::string::npos,
         "result JSON should include linear EXR key");
  EXPECT(json.find("pipeline.png") != std::string::npos,
         "result JSON should include pipeline PNG path");

  const std::string escapedJson =
      LX_demo::lxe_editor::realtimeProfileOutputResultJson(
          std::string("bad") + static_cast<char>(0x01) + "profile",
          LX_demo::lxe_editor::RealtimeProfileOutputResult{
              .linearExrPath = base.parent_path() / "linear.exr",
              .cpuSrgbPngPath = base.parent_path() / "cpu.png",
              .pipelineSrgbPngPath = base.parent_path() / "pipeline.png",
              .metadataPath = base.parent_path() / "render.json",
              .width = output.width,
              .height = output.height,
          });
  EXPECT(escapedJson.find("\\u0001") != std::string::npos,
         "result JSON should escape non-whitespace control characters");
}
} // namespace

int main() {
  testRealtimeRenderLsAndRun();
  testRealtimeProfileOutputHelpersBuildStableJson();
  if (failures != 0) {
    std::cerr << "test_realtime_render_profile_commands failed with "
              << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_realtime_render_profile_commands passed\n";
  return 0;
}
