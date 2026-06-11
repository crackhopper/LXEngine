#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "infra/scene_io/scene_document.hpp"

#include "core/frame_graph/pass.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

void writeTextFile(const std::filesystem::path &path,
                   const std::string &text) {
  std::ofstream out(path);
  if (!out) {
    std::cerr << "[FAIL] failed to open fixture: " << path << '\n';
    std::exit(1);
  }
  out << text;
}

bool loadSceneThrowsFor(const std::string &yamlText,
                        const std::string &expectedDiagnostic) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "lxe_deleted_material_profile_field.scene.yaml";
  writeTextFile(path, yamlText);
  try {
    (void)LX_infra::scene_io::loadSceneDocument(path);
  } catch (const std::runtime_error &error) {
    std::filesystem::remove(path);
    return std::string(error.what()).find(expectedDiagnostic) !=
           std::string::npos;
  }
  std::filesystem::remove(path);
  return false;
}

bool saveSceneThrowsForDeletedExtension(
    const std::string &expectedDiagnostic) {
  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("Programmatic Deleted Extension");

  auto &root = document.mutableRootNode();
  root.name.clear();
  root.parentPath.clear();

  LX_infra::scene_io::SceneNodeDocument child;
  child.nodeName = "mesh_node";
  child.name = "mesh_node";
  child.meshUri = "assets/models/damaged_helmet/DamagedHelmet.gltf";
  child.materialUri = "assets/materials/pbr.material";
  child.materialOfflineYaml = "activeMaterialTag: old-offline\n";
  root.children.push_back(child);

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "lxe_deleted_material_save_extension.scene.yaml";
  try {
    LX_infra::scene_io::saveSceneDocument(path, document);
  } catch (const std::runtime_error &error) {
    std::filesystem::remove(path);
    return std::string(error.what()).find(expectedDiagnostic) !=
           std::string::npos;
  }
  std::filesystem::remove(path);
  return false;
}

void testSceneDocumentRejectsDeletedProfileMaterialField() {
  const std::string outputProfileDeletedField = R"yaml(
scene:
  name: Deleted Material Profile Field
  gameplayCameraPath: /game_cam
  defaultOutputProfile: preview
  outputProfiles:
    preview:
      camera: /game_cam
      width: 16
      height: 16
      materialTag: old-profile
      outputFormat: exr-png
      outDir: artifacts
      backgroundColor: [0.0, 0.0, 0.0]
  offlineRender:
    integrator: software-compute
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
    compareMode: shaded
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
)yaml";
  expect(loadSceneThrowsFor(outputProfileDeletedField,
                            "deleted material selector field"),
         "scene document should reject deleted output profile material field");

  const std::string activeProfileDeletedField = R"yaml(
scene:
  name: Deleted Active Profile Field
  gameplayCameraPath: /game_cam
  defaultOutputProfile: preview
  outputProfiles:
    preview:
      camera: /game_cam
      width: 16
      height: 16
      activeMaterialTag: old-profile
      outputFormat: exr-png
      outDir: artifacts
      backgroundColor: [0.0, 0.0, 0.0]
  offlineRender:
    integrator: software-compute
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
    compareMode: shaded
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
)yaml";
  expect(loadSceneThrowsFor(activeProfileDeletedField,
                            "deleted material selector field"),
         "scene document should reject deleted active profile material field");

  const std::string offlineDeletedField = R"yaml(
scene:
  name: Deleted Offline Material Field
  gameplayCameraPath: /game_cam
  defaultOutputProfile: preview
  outputProfiles:
    preview:
      camera: /game_cam
      width: 16
      height: 16
      outputFormat: exr-png
      outDir: artifacts
      backgroundColor: [0.0, 0.0, 0.0]
  offlineRender:
    integrator: software-compute
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
    materialTag: old-offline
    compareMode: shaded
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
)yaml";
  expect(loadSceneThrowsFor(offlineDeletedField,
                            "deleted material selector field"),
         "scene document should reject deleted offline material field");
}

void testSceneDocumentRejectsDeletedOpaqueMaterialField() {
  const std::string materialOfflineDeletedField = R"yaml(
scene:
  name: Deleted Opaque Material Field
  gameplayCameraPath: /game_cam
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: mesh_node
      name: mesh_node
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: assets/models/damaged_helmet/DamagedHelmet.gltf
      material:
        uri: assets/materials/pbr.material
        offline:
          materialTag: old-offline
)yaml";
  expect(loadSceneThrowsFor(materialOfflineDeletedField,
                            "deleted material selector field"),
         "scene document should reject deleted opaque material field");

  const std::string activeMaterialOfflineDeletedField = R"yaml(
scene:
  name: Deleted Opaque Active Material Field
  gameplayCameraPath: /game_cam
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: mesh_node
      name: mesh_node
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: assets/models/damaged_helmet/DamagedHelmet.gltf
      material:
        uri: assets/materials/pbr.material
        offline:
          activeMaterialTag: old-offline
)yaml";
  expect(loadSceneThrowsFor(activeMaterialOfflineDeletedField,
                            "deleted material selector field"),
         "scene document should reject deleted active opaque material field");
}

void testSceneDocumentRejectsDeletedNodeMaterialsSchema() {
  const std::string deletedMaterialsSchema = R"yaml(
scene:
  name: Deleted Node Materials Schema
  gameplayCameraPath: /game_cam
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: mesh_node
      name: mesh_node
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: assets/models/damaged_helmet/DamagedHelmet.gltf
      materials:
        - tag: old
          uri: assets/materials/pbr.material
)yaml";
  expect(loadSceneThrowsFor(deletedMaterialsSchema,
                            "deleted materials schema"),
         "scene document should reject deleted node materials schema");
}

void testSceneDocumentRejectsDeletedProgrammaticExtensionOnSave() {
  expect(saveSceneThrowsForDeletedExtension("deleted material selector field"),
         "scene document save should reject deleted opaque extension fields");
}

} // namespace

int main() {
  testDamagedHelmetSharedAssetLoadsFullPbr();
  testSceneDocumentRejectsDeletedProfileMaterialField();
  testSceneDocumentRejectsDeletedOpaqueMaterialField();
  testSceneDocumentRejectsDeletedNodeMaterialsSchema();
  testSceneDocumentRejectsDeletedProgrammaticExtensionOnSave();
  std::cout << "test_gltf_scene_asset_loader passed\n";
  return 0;
}
