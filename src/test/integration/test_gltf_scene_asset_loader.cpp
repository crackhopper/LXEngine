#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include "core/frame_graph/pass.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cmath>
#include <cstdlib>
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
  return material &&
         material->getTexture(LX_core::StringID(binding)) != nullptr;
}

void expectNear(float actual, float expected, const char *message) {
  if (std::abs(actual - expected) > 1.0e-5f) {
    std::cerr << "[FAIL] " << message << " actual=" << actual
              << " expected=" << expected << '\n';
    std::exit(1);
  }
}

void testDamagedHelmetSharedAssetLoadsFullPbr() {
  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  expect(found, "DamagedHelmet asset root must be discoverable");

  const auto result = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf",
      "assets/materials/pbr.material");

  expect(result.mesh != nullptr, "shared loader should create mesh");
  expect(result.material != nullptr, "shared loader should create material");
  expect(result.generatedTangents,
         "DamagedHelmet should generate tangents because glTF has no TANGENT");
  expect(result.normalMapEnabled,
         "DamagedHelmet normal map should remain enabled after tangent "
         "generation");
  expect(hasTexture(result.material, "Kd"),
         "shared loader should bind base color as material v2 Kd texture");
  expect(hasTexture(result.material, "normalmap"),
         "shared loader should bind normal map as material v2 normalmap "
         "texture");
  expect(!hasTexture(result.material, "albedoMap"),
         "material v2 should not bind legacy albedoMap");
  expect(!hasTexture(result.material, "normalMap"),
         "material v2 should not bind legacy normalMap");

  expect(result.material->getBsdfType() == "uber",
         "DamagedHelmet material should retain material v2 BSDF type");
  const auto kd = result.material->getMaterialEnvelope(LX_core::StringID("Kd"));
  expect(kd.has_value(), "DamagedHelmet material should retain Kd envelope");
  expect(kd.has_value() &&
             kd->get().kind == LX_core::MaterialEnvelopeKind::Texture,
         "DamagedHelmet Kd should be stored as PBRT texture envelope");
  expect(kd.has_value() &&
             kd->get().valueType == LX_core::MaterialEnvelopeValueType::Rgb,
         "DamagedHelmet Kd texture envelope should retain rgb valueType");

  const auto legacyBaseColor = result.material->readParameterValue(
      LX_core::StringID("MaterialUBO"), LX_core::StringID("baseColorFactor"));
  expect(!legacyBaseColor.has_value(),
         "material v2 should not expose legacy baseColorFactor buffer state");
  expect(result.material->getParameterBufferCount() == 0,
         "material v2 should keep envelope storage without parameter buffers");
}

} // namespace

int main() {
  testDamagedHelmetSharedAssetLoadsFullPbr();
  std::cout << "test_gltf_scene_asset_loader passed\n";
  return 0;
}
