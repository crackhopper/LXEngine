#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include "core/frame_graph/pass.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

bool hasTexture(const LX_core::MaterialInstanceSharedPtr &material,
                const char *binding) {
  return material && material->getTexture(LX_core::StringID(binding)) != nullptr;
}

void testDamagedHelmetSharedAssetLoadsFullPbr() {
  const bool found = LX_core::cdToWhereAssetsExist(
      "models/damaged_helmet/DamagedHelmet.gltf");
  expect(found, "DamagedHelmet asset root must be discoverable");

  const auto result = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf");

  expect(result.mesh != nullptr, "shared loader should create mesh");
  expect(result.material != nullptr, "shared loader should create material");
  expect(result.generatedTangents,
         "DamagedHelmet should generate tangents because glTF has no TANGENT");
  expect(result.normalMapEnabled,
         "DamagedHelmet normal map should remain enabled after tangent "
         "generation");
  expect(hasTexture(result.material, "albedoMap"),
         "shared loader should bind albedoMap");
  expect(hasTexture(result.material, "normalMap"),
         "shared loader should bind normalMap");
  expect(hasTexture(result.material, "metallicRoughnessMap"),
         "shared loader should bind metallicRoughnessMap");
  expect(hasTexture(result.material, "aoMap"),
         "shared loader should bind aoMap");
  expect(hasTexture(result.material, "emissiveMap"),
         "shared loader should bind emissiveMap");
}

} // namespace

int main() {
  testDamagedHelmetSharedAssetLoadsFullPbr();
  std::cout << "test_gltf_scene_asset_loader passed\n";
  return 0;
}
