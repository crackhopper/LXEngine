#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/scene_io/scene_document.hpp"
#include "tools/lxe_offline_render/offline_render_cli.hpp"

#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
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

void testCliParsesOfflineOverrides() {
  const std::vector<std::string> args{
      "--scene",      "assets/scenes/ibl_metal_sphere.scene.yaml",
      "--profile",    "mvp",
      "--width",      "64",
      "--height",     "36",
      "--samples",    "2",
      "--max-bounce", "3",
      "--seed",       "11",
      "--out",        "artifacts/offline/test"};
  const auto options =
      LX_tools::offline_render::parseOfflineRenderCliArguments(args);
  EXPECT(options.scenePath == "assets/scenes/ibl_metal_sphere.scene.yaml",
         "scene path should parse");
  EXPECT(options.overrides.profileName == "mvp", "profile should parse");
  EXPECT(options.overrides.width == 64u, "width should parse");
  EXPECT(options.overrides.height == 36u, "height should parse");
  EXPECT(options.overrides.samples == 2u, "samples should parse");
  EXPECT(options.overrides.maxBounce == 3u, "maxBounce should parse");
  EXPECT(options.overrides.seed == 11u, "seed should parse");
  EXPECT(options.overrides.outputPath == "artifacts/offline/test",
         "output path should parse");
}

void testCliRejectsMaxDepth() {
  bool rejected = false;
  try {
    (void)LX_tools::offline_render::parseOfflineRenderCliArguments(
        {"--scene", "assets/scenes/ibl_metal_sphere.scene.yaml",
         "--max-depth", "4"});
  } catch (const std::exception &e) {
    rejected = std::string(e.what()).find("--max-depth") != std::string::npos;
  }
  EXPECT(rejected, "--max-depth should be rejected");
}

void testCliRejectsCamera() {
  bool rejected = false;
  try {
    (void)LX_tools::offline_render::parseOfflineRenderCliArguments(
        {"--scene", "assets/scenes/ibl_metal_sphere.scene.yaml", "--camera",
         "/game_cam"});
  } catch (const std::exception &e) {
    rejected = std::string(e.what()).find("--camera") != std::string::npos;
  }
  EXPECT(rejected, "--camera should be rejected");
}

void testSoftwareComputeProfileRendersAndRejectsHardwareRt() {
  const std::filesystem::path scenePath =
      "assets/scenes/realtime_offline_compare_diagnostic.scene.yaml";
  const auto document = LX_infra::scene_io::loadSceneDocument(scenePath);
  const auto resolved = LX_core::offline::resolveRenderProfileDocument(
      document.renderProfileDocument(),
      LX_core::offline::RenderProfileCliOverrides{
          .profileName = std::string("preview"),
          .width = 8u,
          .height = 8u,
          .samples = 1u,
      });
  EXPECT(resolved.offline.integrator == "software-compute",
         "diagnostic scene should use software-compute integrator");

  LX_infra::offline::OfflineAssetResolver resolver(scenePath);
  LX_infra::offline::OfflineSceneLoader loader(resolver);
  auto loaded = loader.load(document, resolved.output.cameraPath);

  LX_core::offline::OfflineRenderJob job;
  job.scene = std::move(loaded.table);
  job.output = resolved.output;
  job.offline = resolved.offline;
  job.profileName = resolved.profileName;

  LX_core::offline::validateOfflineRenderJob(job);
  LX_core::backend::offline::VulkanOfflineRenderer renderer;
  const auto image = renderer.render(job);
  EXPECT(image.width == 8u && image.height == 8u,
         "software-compute render should use overridden dimensions");
  EXPECT(!image.rgba.empty(), "software-compute render should produce pixels");
  bool finite = true;
  for (const float value : image.rgba) {
    finite = finite && std::isfinite(value);
  }
  EXPECT(finite, "software-compute render should produce finite pixels");

  job.offline.integrator = "hardware-ray-tracing";
  bool rejected = false;
  try {
    (void)renderer.render(job);
  } catch (const std::exception &error) {
    rejected =
        std::string(error.what()).find("unsupported offline integrator: "
                                      "hardware-ray-tracing") !=
        std::string::npos;
  }
  EXPECT(rejected, "hardware-ray-tracing should fail clearly until implemented");
}

} // namespace

int main() {
  testCliParsesOfflineOverrides();
  testCliRejectsMaxDepth();
  testCliRejectsCamera();
  testSoftwareComputeProfileRendersAndRejectsHardwareRt();
  if (failures != 0) {
    std::cerr << "test_offline_render_cli failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_render_cli passed\n";
  return 0;
}
