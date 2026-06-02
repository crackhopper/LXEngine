#include "tools/lxe_offline_render/offline_render_cli.hpp"

#include <iostream>
#include <string>
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

} // namespace

int main() {
  testCliParsesOfflineOverrides();
  testCliRejectsMaxDepth();
  testCliRejectsCamera();
  if (failures != 0) {
    std::cerr << "test_offline_render_cli failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_render_cli passed\n";
  return 0;
}
