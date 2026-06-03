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
  return material && material->getTexture(LX_core::StringID(binding)) != nullptr;
}

void expectNear(float actual, float expected, const char *message) {
  if (std::abs(actual - expected) > 1.0e-5f) {
    std::cerr << "[FAIL] " << message << " actual=" << actual
              << " expected=" << expected << '\n';
    std::exit(1);
  }
}

LX_core::MaterialParameterValue
readMaterialUboParameter(const LX_core::MaterialInstanceSharedPtr &material,
                         const char *member) {
  const auto value = material->readParameterValue(
      LX_core::StringID("MaterialUBO"), LX_core::StringID(member));
  expect(value.has_value(), "PBR material parameter should be readable");
  return *value;
}

void testDamagedHelmetSharedAssetLoadsFullPbr() {
  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
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

  const auto baseColorFactor =
      readMaterialUboParameter(result.material, "baseColorFactor");
  expect(baseColorFactor.type == LX_core::MaterialParameterValueType::Vec4,
         "baseColorFactor should be stored as vec4");
  expectNear(baseColorFactor.vectorValue.x, 1.0f,
             "DamagedHelmet baseColorFactor.r should load");
  expectNear(baseColorFactor.vectorValue.y, 1.0f,
             "DamagedHelmet baseColorFactor.g should load");
  expectNear(baseColorFactor.vectorValue.z, 1.0f,
             "DamagedHelmet baseColorFactor.b should load");
  expectNear(baseColorFactor.vectorValue.w, 1.0f,
             "DamagedHelmet baseColorFactor.a should load");

  const auto metallicFactor =
      readMaterialUboParameter(result.material, "metallicFactor");
  expect(metallicFactor.type == LX_core::MaterialParameterValueType::Float,
         "metallicFactor should be stored as float");
  expectNear(metallicFactor.floatValue, 1.0f,
             "DamagedHelmet metallicFactor should load");

  const auto roughnessFactor =
      readMaterialUboParameter(result.material, "roughnessFactor");
  expect(roughnessFactor.type == LX_core::MaterialParameterValueType::Float,
         "roughnessFactor should be stored as float");
  expectNear(roughnessFactor.floatValue, 1.0f,
             "DamagedHelmet roughnessFactor should load");

  const auto ao = readMaterialUboParameter(result.material, "ao");
  expect(ao.type == LX_core::MaterialParameterValueType::Float,
         "AO should be stored as float");
  expectNear(ao.floatValue, 1.0f,
             "DamagedHelmet AO scalar should default to fully unoccluded");
}

} // namespace

int main() {
  testDamagedHelmetSharedAssetLoadsFullPbr();
  std::cout << "test_gltf_scene_asset_loader passed\n";
  return 0;
}
