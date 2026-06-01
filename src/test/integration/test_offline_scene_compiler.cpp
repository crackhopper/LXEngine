#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_compiler.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
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

void testIblMetalSphereCompilesToOfflineIr() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "ibl_metal_sphere.scene.yaml";
  const auto document = LX_infra::scene_io::loadSceneDocument(scenePath);
  LX_infra::offline::OfflineSceneCompiler compiler{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto scene = compiler.compile(document, "/game_cam");

  EXPECT(scene.cameraPath == "/game_cam", "requested camera should compile");
  EXPECT(scene.meshes.size() == 2, "builtin sphere and plane meshes should remain distinct");
  EXPECT(scene.instances.size() == 2, "scene should keep mesh instances");
  EXPECT(scene.materials.size() == 2, "scene should keep per-node material IR");
  EXPECT(!scene.directionalLights.empty(), "directional light should compile");
  EXPECT(scene.environment.enabled, "environment should compile");
  EXPECT(scene.instances[0].meshIndex != scene.instances[1].meshIndex,
         "IR should preserve mesh references instead of flattening early");
}

} // namespace

int main() {
  testIblMetalSphereCompilesToOfflineIr();
  if (failures != 0) {
    std::cerr << "test_offline_scene_compiler failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_scene_compiler passed\n";
  return 0;
}
