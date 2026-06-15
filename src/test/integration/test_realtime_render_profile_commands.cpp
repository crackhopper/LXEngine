#include "backend/vulkan/vulkan_realtime_renderer.hpp"
#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene.hpp"
#include "infra/image/rgba_image_io.hpp"
#include "editor/app/editor_session.hpp"
#include "editor/app/editor_state.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "editor/project/debug_render_export.hpp"
#include "editor/project/realtime_render_profile.hpp"
#include "editor/runtime/scene_interaction_controller.hpp"
#include "editor/runtime/scene_view_rect.hpp"

#include <cmath>
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

[[nodiscard]] bool nearlyEqual(const float a, const float b,
                               const float eps = 1.0e-4f) {
  return std::abs(a - b) <= eps;
}

[[nodiscard]] bool vec3NearlyEqual(const LX_core::Vec3f &a,
                                   const LX_core::Vec3f &b) {
  return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) &&
         nearlyEqual(a.z, b.z);
}

[[nodiscard]] bool vec4NearlyEqual(const LX_core::Vec4f &a,
                                   const LX_core::Vec4f &b) {
  return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) &&
         nearlyEqual(a.z, b.z) && nearlyEqual(a.w, b.w);
}

[[nodiscard]] bool mat4NearlyEqual(const LX_core::Mat4f &a,
                                   const LX_core::Mat4f &b) {
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!nearlyEqual(a(row, col), b(row, col), 1.0e-3f)) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool
debugViewNearlyEqual(const LX_core::DirectionalShadowCascadeDebugView &a,
                     const LX_core::DirectionalShadowCascadeDebugView &b) {
  return vec3NearlyEqual(a.eye, b.eye) && vec3NearlyEqual(a.target, b.target) &&
         vec3NearlyEqual(a.up, b.up) && nearlyEqual(a.left, b.left, 1.0e-3f) &&
         nearlyEqual(a.right, b.right, 1.0e-3f) &&
         nearlyEqual(a.bottom, b.bottom, 1.0e-3f) &&
         nearlyEqual(a.top, b.top, 1.0e-3f) &&
         nearlyEqual(a.nearPlane, b.nearPlane, 1.0e-3f) &&
         nearlyEqual(a.farPlane, b.farPlane, 1.0e-3f);
}

[[nodiscard]] LX_core::CameraComponent &
makeShadowTestCamera(LX_core::Scene &scene, const char *name,
                     const LX_core::Vec3f &eye, const LX_core::Vec3f &target) {
  auto node = LX_core::SceneNode::create(name);
  auto camera = node->addComponent<LX_core::CameraComponent>();
  scene.addCamera(node);
  camera->get().lookAt(eye, target, {0.0f, 1.0f, 0.0f});
  camera->get().setNearPlane(0.25f);
  camera->get().setFarPlane(120.0f);
  return camera->get();
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

  const std::string json = LX_demo::lxe_editor::realtimeProfileOutputResultJson(
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

  static_assert(
      std::is_same_v<
          decltype(std::declval<VulkanRenderer &>()
                       .generateRealtimeProfileOutput(
                           std::declval<LX_core::SceneSharedPtr>(),
                           std::declval<
                               const LX_core::offline::OutputProfile &>(),
                           std::declval<const std::filesystem::path &>())),
          VulkanRealtimeProfileOutputResult>);
  static_assert(
      std::is_same_v<
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
  using LX_core::backend::VulkanDebugColorTransferPassRecord;
  using LX_core::backend::VulkanDebugColorTransferExportRequest;
  using LX_core::backend::VulkanDebugColorTransferExportResult;
  using LX_core::backend::VulkanDebugColorTransferPreviewTransform;
  using LX_core::backend::VulkanDebugColorTransferTargetRecord;
  using LX_core::backend::VulkanRealtimeRenderer;
  using LX_core::backend::VulkanRenderer;

  static_assert(
      std::is_same_v<
          decltype(std::declval<VulkanRenderer &>().exportDebugColorTransfer(
              std::declval<const VulkanDebugColorTransferExportRequest &>())),
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
  result.graphUri = "assets/render_paths/debug_color_transfer.render-path.yaml";
  result.cameraPath = request.cameraPath;
  result.previewTransform = VulkanDebugColorTransferPreviewTransform{
      .kind = "cpu-tone-mapped-png",
      .toneMappingMode = "aces",
      .exposure = 1.0f,
      .gamma = 2.2f,
  };
  result.manifestPath = "manifest.json";
  result.targets.push_back(VulkanDebugColorTransferTargetRecord{
      .name = "debug.ramp.srgb",
      .path = "ramp_srgb_attachment.png",
      .format = "R8G8B8A8_SRGB",
      .width = 64,
      .height = 64,
  });
  result.passes.push_back(VulkanDebugColorTransferPassRecord{
      .pass = "DebugSrgbAttachment",
      .target = "debug.final.srgb",
      .shader = "assets://shaders/glsl/render_paths/debug_color_transfer_copy.frag",
      .attachmentFormat = "RGBA8Srgb",
      .pipelineColorFormat = "RGBA8Srgb",
      .outputEncodingMode = "Linear",
  });
  EXPECT(result.graphUri.find("debug_color_transfer") != std::string::npos,
         "debug export result should expose render-path graph URI");
  EXPECT(result.cameraPath == "/editor_cam",
         "debug export result should expose camera path");
  EXPECT(result.previewTransform.toneMappingMode == "aces",
         "debug export result should expose preview tone mapping mode");
  EXPECT(result.passes.size() == 1,
         "debug export result should expose pass records");
  EXPECT(result.passes.front().target == "debug.final.srgb",
         "debug export pass record should expose logical target");
  EXPECT(result.passes.front().outputEncodingMode == "Linear",
         "debug export pass record should expose shader output encoding");
  EXPECT(result.manifestPath.filename() == "manifest.json",
         "debug export result should expose manifest path");
  EXPECT(result.targets.size() == 1,
         "debug export result should expose target records");
}

void testDebugColorTransferExportResultJson() {
  LX_core::backend::VulkanDebugColorTransferExportResult result;
  result.manifestPath = "artifacts/debug/color-transfer/manifest.json";
  result.outputDirectory = "artifacts/debug/color-transfer";
  result.targets.push_back(
      LX_core::backend::VulkanDebugColorTransferTargetRecord{
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
  result.targets.push_back(
      LX_core::backend::VulkanDebugColorTransferTargetRecord{
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
      []() { LX_infra::image::writeRawRgba8Png("unused.png", 0, 1, {}); },
      "invalid raw RGBA8 PNG payload",
      "raw PNG writer should reject zero width");
  expectRuntimeErrorContaining(
      []() {
        LX_infra::image::writeRawRgba8Png("unused.png", 2, 2,
                                          std::vector<unsigned char>(4));
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

void testDirectionalLightRuntimeShadowStateRestoresFromSnapshot() {
  auto scene = LX_core::Scene::create(LX_core::SceneNode::create("root"));
  LX_core::CameraComponent &mainCamera = makeShadowTestCamera(
      *scene, "main_cam", {3.0f, 2.0f, 8.0f}, {0.0f, 0.0f, 0.0f});
  LX_core::CameraComponent &debugCamera = makeShadowTestCamera(
      *scene, "debug_cam", {-6.0f, 5.0f, 4.0f}, {0.5f, 0.5f, 0.0f});

  LX_core::DirectionalLight light;
  light.setShadowCascadeCount(4);
  light.updateShadowCascadesForCamera(mainCamera);
  const std::unique_ptr<LX_core::LightBase> snapshotBase = light.cloneUnique();
  const auto *snapshot =
      dynamic_cast<const LX_core::DirectionalLight *>(snapshotBase.get());
  EXPECT(snapshot != nullptr,
         "directional light clone should preserve concrete type");
  if (snapshot == nullptr) {
    return;
  }

  const auto expectedParam = snapshot->getDirectionalUBO().param;
  const auto expectedDebugView = snapshot->getShadowCascadeDebugView(0);

  light.updateShadowCascadesForCamera(debugCamera);
  EXPECT(!mat4NearlyEqual(light.getDirectionalUBO().param.cascadeViewProj[0],
                          expectedParam.cascadeViewProj[0]),
         "debug camera should mutate directional cascade state");

  light.restoreShadowCascadeStateFrom(*snapshot);
  const auto &restoredParam = light.getDirectionalUBO().param;
  EXPECT(mat4NearlyEqual(restoredParam.shadowViewProj,
                         expectedParam.shadowViewProj),
         "restore should recover active shadow matrix");
  EXPECT(mat4NearlyEqual(restoredParam.cascadeViewProj[0],
                         expectedParam.cascadeViewProj[0]),
         "restore should recover cascade matrix");
  EXPECT(
      vec4NearlyEqual(restoredParam.cascadeSplits, expectedParam.cascadeSplits),
      "restore should recover cascade splits");
  EXPECT(vec4NearlyEqual(restoredParam.cascadeDepthRanges,
                         expectedParam.cascadeDepthRanges),
         "restore should recover cascade depth ranges");
  const auto restoredDebugView = light.getShadowCascadeDebugView(0);
  EXPECT(restoredDebugView.has_value() == expectedDebugView.has_value(),
         "restore should recover cascade debug-view validity");
  if (restoredDebugView.has_value() && expectedDebugView.has_value()) {
    EXPECT(debugViewNearlyEqual(*restoredDebugView, *expectedDebugView),
           "restore should recover cascade debug-view data");
  }
}

void testDebugColorTransferExportRejectsUninitializedRendererBeforeStub() {
  LX_core::backend::VulkanRealtimeRenderer renderer;
  expectRuntimeErrorContaining(
      [&renderer]() {
        renderer.exportDebugColorTransfer(
            LX_core::backend::VulkanDebugColorTransferExportRequest{
                .outputDirectory = "artifacts/debug/color-transfer"});
      },
      "renderer is not initialized",
      "debug color transfer export should not expose the old backend stub");
}

void testDebugColorTransferExportRejectsCollapsedProbeExtentBeforeRendererWork() {
  LX_core::backend::VulkanRealtimeRenderer renderer;
  expectRuntimeErrorContaining(
      [&renderer]() {
        renderer.exportDebugColorTransfer(
            LX_core::backend::VulkanDebugColorTransferExportRequest{
                .outputDirectory = "artifacts/debug/color-transfer",
                .width = 4,
                .height = 1});
      },
      "at least 10x1",
      "debug color transfer export should reject ramp probe extents that "
      "collapse fixed probe bands before touching renderer state");
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

  const auto response =
      bus.dispatch("render debug export-path color-transfer /editor_cam "
                   "artifacts/debug/color-transfer");

  EXPECT(response.ok,
         "render debug export-path color-transfer should succeed with hook");
  EXPECT(hookCalled,
         "render debug export-path color-transfer should call export hook");
  EXPECT(response.message == "debug color transfer exported: "
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

  const auto response = bus.dispatch("render debug export-path color-transfer");

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

void testRenderDebugColorTransferCommandReportsBackendFailure() {
  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks{
      .exportColorTransferPath =
          [](const LX_core::backend::VulkanDebugColorTransferExportRequest &)
          -> LX_core::backend::VulkanDebugColorTransferExportResult {
        throw std::runtime_error(
            "simulated debug color transfer backend failure");
      }};
  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const auto response = bus.dispatch("render debug export-path color-transfer");

  EXPECT(!response.ok,
         "render debug export-path color-transfer should report hook errors");
  EXPECT(response.message == "simulated debug color transfer backend failure",
         "debug export should propagate backend failure diagnostics");
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
  testDirectionalLightRuntimeShadowStateRestoresFromSnapshot();
  testDebugColorTransferExportRejectsUninitializedRendererBeforeStub();
  testDebugColorTransferExportRejectsCollapsedProbeExtentBeforeRendererWork();
  testRenderDebugColorTransferCommandUsesExportHook();
  testRenderDebugColorTransferCommandReportsUnavailableHook();
  testRenderDebugColorTransferCommandReportsUsageError();
  testRenderDebugColorTransferCommandReportsBackendFailure();
  if (failures != 0) {
    std::cerr << "test_realtime_render_profile_commands failed with "
              << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_realtime_render_profile_commands passed\n";
  return 0;
}
