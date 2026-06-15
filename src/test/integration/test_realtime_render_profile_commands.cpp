#include "backend/vulkan/vulkan_renderer.hpp"
#include "backend/vulkan/vulkan_realtime_renderer.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/app/editor_state.hpp"
#include "editor/app/editor_session.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/scene/scene.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "editor/project/debug_render_export.hpp"
#include "editor/project/realtime_render_profile.hpp"
#include "editor/runtime/scene_interaction_controller.hpp"
#include "editor/runtime/scene_view_rect.hpp"
#include "infra/image/rgba_image_io.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

template <typename Fn>
void expectRuntimeErrorContaining(Fn &&fn, std::string_view expected,
                                  std::string_view message) {
  try {
    fn();
  } catch (const std::runtime_error &err) {
    const std::string what = err.what();
    EXPECT(what.find(expected) != std::string::npos, message);
    return;
  } catch (...) {
    EXPECT(false, message);
    return;
  }
  EXPECT(false, message);
}

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

void testVulkanRealtimeProfileOutputApiShape() {
  using LX_core::backend::VulkanRealtimeProfileOutputResult;
  using LX_core::backend::VulkanRealtimeRenderer;
  using LX_core::backend::VulkanRenderer;

  static_assert(std::is_same_v<
                decltype(std::declval<VulkanRenderer &>()
                             .generateRealtimeProfileOutput(
                                 std::declval<LX_core::SceneSharedPtr>(),
                                 std::declval<
                                     const LX_core::offline::OutputProfile &>(),
                                 std::declval<const std::filesystem::path &>())),
                VulkanRealtimeProfileOutputResult>);
  static_assert(std::is_same_v<
                decltype(std::declval<VulkanRealtimeRenderer &>()
                             .generateRealtimeProfileOutput(
                                 std::declval<LX_core::SceneSharedPtr>(),
                                 std::declval<
                                     const LX_core::offline::OutputProfile &>(),
                                 std::declval<const std::filesystem::path &>())),
                VulkanRealtimeProfileOutputResult>);

  VulkanRealtimeProfileOutputResult result{
      .linearExrPath = "linear.exr",
      .cpuSrgbPngPath = "cpu_srgb.png",
      .pipelineSrgbPngPath = {},
      .metadataPath = "render.json",
      .width = 64,
      .height = 32,
  };
  EXPECT(result.linearExrPath.filename() == "linear.exr",
         "result exposes linear EXR path");
  EXPECT(result.cpuSrgbPngPath.filename() == "cpu_srgb.png",
         "result exposes CPU sRGB PNG path");
  EXPECT(result.pipelineSrgbPngPath.empty(),
         "pipeline sRGB PNG can be unavailable without a fake path");
  EXPECT(result.metadataPath.filename() == "render.json",
         "result exposes metadata path");
  EXPECT(result.width == 64 && result.height == 32,
         "result exposes output extent");
}

void testDebugColorTransferExportApiShape() {
  using LX_core::backend::VulkanDebugColorTransferExportRequest;
  using LX_core::backend::VulkanDebugColorTransferExportResult;
  using LX_core::backend::VulkanDebugColorTransferTargetRecord;
  using LX_core::backend::VulkanRenderer;
  using LX_core::backend::VulkanRealtimeRenderer;

  static_assert(std::is_same_v<
                decltype(std::declval<VulkanRenderer &>()
                             .exportDebugColorTransfer(
                                 std::declval<
                                     const VulkanDebugColorTransferExportRequest
                                         &>())),
                VulkanDebugColorTransferExportResult>);
  static_assert(std::is_same_v<
                decltype(std::declval<VulkanRealtimeRenderer &>()
                             .exportDebugColorTransfer(
                                 std::declval<
                                     const VulkanDebugColorTransferExportRequest
                                         &>())),
                VulkanDebugColorTransferExportResult>);

  VulkanDebugColorTransferExportRequest request{
      .cameraPath = "/editor_cam",
      .outputDirectory = "artifacts/debug/color-transfer",
  };
  EXPECT(request.cameraPath == "/editor_cam",
         "debug export request should expose camera path");
  EXPECT(request.outputDirectory.filename() == "color-transfer",
         "debug export request should expose output directory");

  VulkanDebugColorTransferExportResult result;
  result.manifestPath = "manifest.json";
  result.targets.push_back(VulkanDebugColorTransferTargetRecord{
      .name = "debug.ramp.srgb",
      .path = "ramp_srgb_attachment.png",
      .format = "R8G8B8A8_SRGB",
      .width = 64,
      .height = 64,
  });
  EXPECT(result.manifestPath.filename() == "manifest.json",
         "debug export result should expose manifest path");
  EXPECT(result.targets.size() == 1,
         "debug export result should expose target records");
}

void testDebugColorTransferExportResultJson() {
  LX_core::backend::VulkanDebugColorTransferExportResult result;
  result.manifestPath = "artifacts/debug/color-transfer/manifest.json";
  result.outputDirectory = "artifacts/debug/color-transfer";
  result.targets.push_back(LX_core::backend::VulkanDebugColorTransferTargetRecord{
      .name = "debug.final.srgb",
      .path = "artifacts/debug/color-transfer/srgb_attachment.png",
      .format = "R8G8B8A8_SRGB",
      .width = 64,
      .height = 32,
  });

  const std::string json =
      LX_demo::lxe_editor::debugColorTransferExportResultJson(result);
  EXPECT(json.find("\"manifestPath\"") != std::string::npos,
         "debug export JSON should expose manifest path");
  EXPECT(json.find("srgb_attachment.png") != std::string::npos,
         "debug export JSON should expose target path");
  EXPECT(json.find("\"width\":64") != std::string::npos,
         "debug export JSON should expose width");
}

void testDebugColorTransferExportResultJsonEscapesControlBytes() {
  LX_core::backend::VulkanDebugColorTransferExportResult result;
  result.manifestPath =
      std::filesystem::path("manifest") /
      (std::string("bad") + static_cast<char>(0x01) + ".json");
  result.outputDirectory = std::filesystem::path("out") / "dir";
  result.targets.push_back(LX_core::backend::VulkanDebugColorTransferTargetRecord{
      .name = std::string("quote\" slash\\ nl\n cr\r tab\t bs") +
              static_cast<char>(0x08) + " ff" + static_cast<char>(0x0c) +
              " low" + static_cast<char>(0x1f),
      .path = "target.png",
      .format = "R8G8B8A8_SRGB",
      .width = 4,
      .height = 2,
  });

  const std::string json =
      LX_demo::lxe_editor::debugColorTransferExportResultJson(result);
  EXPECT(json.find("\\\"") != std::string::npos,
         "debug export JSON should escape quotes");
  EXPECT(json.find("\\\\") != std::string::npos,
         "debug export JSON should escape backslashes");
  EXPECT(json.find("\\n") != std::string::npos,
         "debug export JSON should escape newlines");
  EXPECT(json.find("\\r") != std::string::npos,
         "debug export JSON should escape carriage returns");
  EXPECT(json.find("\\t") != std::string::npos,
         "debug export JSON should escape tabs");
  EXPECT(json.find("\\b") != std::string::npos,
         "debug export JSON should escape backspace");
  EXPECT(json.find("\\f") != std::string::npos,
         "debug export JSON should escape form feed");
  EXPECT(json.find("\\u0001") != std::string::npos,
         "debug export JSON should escape low control bytes in paths");
  EXPECT(json.find("\\u001f") != std::string::npos,
         "debug export JSON should escape generic low control bytes");
}

void testRawRgba8PngRejectsInvalidPayloadsBeforeStb() {
  expectRuntimeErrorContaining(
      []() {
        LX_infra::image::writeRawRgba8Png("unused.png", 0, 1, {});
      },
      "invalid raw RGBA8 PNG payload",
      "raw PNG writer should reject zero width");
  expectRuntimeErrorContaining(
      []() {
        LX_infra::image::writeRawRgba8Png(
            "unused.png", 2, 2, std::vector<unsigned char>(4));
      },
      "invalid raw RGBA8 PNG payload",
      "raw PNG writer should reject payload size mismatch");
  expectRuntimeErrorContaining(
      []() {
        const u32 tooWide =
            static_cast<u32>(std::numeric_limits<int>::max()) + 1u;
        LX_infra::image::writeRawRgba8Png("unused.png", tooWide, 1, {});
      },
      "invalid raw RGBA8 PNG payload",
      "raw PNG writer should reject dimensions outside stb int bounds");
  expectRuntimeErrorContaining(
      []() {
        const u32 overflowDimension =
            (static_cast<u32>(std::numeric_limits<int>::max()) + 1u);
        LX_infra::image::writeRawRgba8Png("unused.png", overflowDimension,
                                          overflowDimension, {});
      },
      "invalid raw RGBA8 PNG payload",
      "raw PNG writer should reject expected-size overflow");
}

void testRenderDebugColorTransferCommandUsesExportHook() {
  LX_core::CommandBus bus;
  bool hookCalled = false;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks{
      .exportColorTransferPath =
          [&hookCalled](
              const LX_core::backend::VulkanDebugColorTransferExportRequest
                  &request) {
            hookCalled = true;
            EXPECT(request.cameraPath.has_value(),
                   "debug export should pass camera path to hook");
            EXPECT(*request.cameraPath == "/editor_cam",
                   "debug export should preserve camera path");
            EXPECT(request.outputDirectory.generic_string() ==
                       "artifacts/debug/color-transfer",
                   "debug export should pass output directory to hook");

            LX_core::backend::VulkanDebugColorTransferExportResult result;
            result.manifestPath =
                "artifacts/debug/color-transfer/manifest.json";
            result.outputDirectory = "artifacts/debug/color-transfer";
            result.targets.push_back(
                LX_core::backend::VulkanDebugColorTransferTargetRecord{
                    .name = "debug.final.srgb",
                    .path = "artifacts/debug/color-transfer/"
                            "srgb_attachment.png",
                    .format = "R8G8B8A8_SRGB",
                    .width = 64,
                    .height = 32,
                });
            return result;
          }};
  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const auto response = bus.dispatch(
      "render debug export-path color-transfer /editor_cam "
      "artifacts/debug/color-transfer");

  EXPECT(response.ok,
         "render debug export-path color-transfer should succeed with hook");
  EXPECT(hookCalled,
         "render debug export-path color-transfer should call export hook");
  EXPECT(response.message ==
             "debug color transfer exported: "
             "artifacts/debug/color-transfer/manifest.json",
         "debug export response should name manifest path");
  EXPECT(response.structured.find("\"manifestPath\"") != std::string::npos,
         "debug export response should return structured manifest JSON");
  EXPECT(response.structured.find("srgb_attachment.png") != std::string::npos,
         "debug export response should return target JSON");
}

void testRenderDebugColorTransferCommandReportsUnavailableHook() {
  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks;
  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const auto response = bus.dispatch(
      "render debug export-path color-transfer");

  EXPECT(!response.ok,
         "render debug export-path color-transfer should fail without hook");
  EXPECT(response.message ==
             "render debug export-path color-transfer unavailable",
         "unavailable debug export should report exact diagnostic");
}

void testRenderDebugColorTransferCommandReportsUsageError() {
  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks;
  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const auto response = bus.dispatch(
      "render debug export-path color-transfer /editor_cam out extra");

  EXPECT(!response.ok,
         "render debug export-path color-transfer should reject extra args");
  EXPECT(response.message ==
             "usage: render debug export-path color-transfer [camera-path] "
             "[out-dir]",
         "extra debug export args should report exact usage");
}

void testRenderDebugColorTransferCommandReportsStubException() {
  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks{
      .exportColorTransferPath =
          [](const LX_core::backend::VulkanDebugColorTransferExportRequest &)
              -> LX_core::backend::VulkanDebugColorTransferExportResult {
        throw std::runtime_error(
            "debug color transfer export is not implemented");
      }};
  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const auto response = bus.dispatch(
      "render debug export-path color-transfer");

  EXPECT(!response.ok,
         "render debug export-path color-transfer should report hook errors");
  EXPECT(response.message ==
             "debug color transfer export is not implemented",
         "debug export should propagate backend stub diagnostic");
}
} // namespace

int main() {
  testRealtimeRenderLsAndRun();
  testRealtimeProfileOutputHelpersBuildStableJson();
  testVulkanRealtimeProfileOutputApiShape();
  testDebugColorTransferExportApiShape();
  testDebugColorTransferExportResultJson();
  testDebugColorTransferExportResultJsonEscapesControlBytes();
  testRawRgba8PngRejectsInvalidPayloadsBeforeStb();
  testRenderDebugColorTransferCommandUsesExportHook();
  testRenderDebugColorTransferCommandReportsUnavailableHook();
  testRenderDebugColorTransferCommandReportsUsageError();
  testRenderDebugColorTransferCommandReportsStubException();
  if (failures != 0) {
    std::cerr << "test_realtime_render_profile_commands failed with "
              << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_realtime_render_profile_commands passed\n";
  return 0;
}
