#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "infra/mesh_loader/gltf_mesh_loader.hpp"
#include "infra/scene_io/scene_document.hpp"

#include "core/frame_graph/pass.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void writeTextFile(const std::filesystem::path &path, const std::string &text);

template <typename T>
void appendBinary(std::vector<unsigned char> &bytes, const T &value) {
  const auto *begin = reinterpret_cast<const unsigned char *>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(T));
}

std::filesystem::path writeFullPbrFixture() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "lxe_standard_pbr_gltf_fixture";
  std::filesystem::create_directories(dir);

  std::vector<unsigned char> bytes;
  const float positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f};
  const float normals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                           0.0f, 0.0f, 1.0f};
  const float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  const std::uint16_t indices[] = {0, 1, 2};
  for (const float value : positions) {
    appendBinary(bytes, value);
  }
  for (const float value : normals) {
    appendBinary(bytes, value);
  }
  for (const float value : uvs) {
    appendBinary(bytes, value);
  }
  for (const std::uint16_t value : indices) {
    appendBinary(bytes, value);
  }

  const std::filesystem::path binPath = dir / "fixture.bin";
  std::ofstream bin(binPath, std::ios::binary);
  if (!bin) {
    std::cerr << "[FAIL] failed to open fixture bin: " << binPath << '\n';
    std::exit(1);
  }
  bin.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));

  const std::filesystem::path gltfPath = dir / "full_pbr.gltf";
  writeTextFile(
      gltfPath,
      std::string(R"json({
  "asset": { "version": "2.0" },
  "buffers": [
    { "uri": "fixture.bin", "byteLength": )json") +
          std::to_string(bytes.size()) + R"json( }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "images": [
    { "uri": "albedo.png" },
    { "uri": "metal_rough.png" },
    { "uri": "normal.png" },
    { "uri": "ao.png" },
    { "uri": "emissive.png" }
  ],
  "textures": [
    { "source": 0 },
    { "source": 1 },
    { "source": 2 },
    { "source": 3 },
    { "source": 4 }
  ],
  "materials": [
    {
      "name": "Full PBR",
      "alphaMode": "MASK",
      "alphaCutoff": 0.35,
      "emissiveFactor": [0.8, 0.9, 1.0],
      "emissiveTexture": { "index": 4 },
      "normalTexture": { "index": 2 },
      "occlusionTexture": { "index": 3 },
      "pbrMetallicRoughness": {
        "baseColorFactor": [0.2, 0.3, 0.4, 0.5],
        "metallicFactor": 0.6,
        "roughnessFactor": 0.7,
        "baseColorTexture": { "index": 0 },
        "metallicRoughnessTexture": { "index": 1 }
      }
    }
  ],
  "meshes": [
    {
      "primitives": [
        {
          "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
          "indices": 3,
          "material": 0
        }
      ]
    }
  ]
})json");
  return gltfPath;
}

LX_core::MaterialParameterEnvelope
requireEnvelope(const LX_core::MaterialInstanceSharedPtr &material,
                const char *parameterName) {
  const auto envelope =
      material->getMaterialEnvelope(LX_core::StringID(parameterName));
  expect(envelope.has_value(), "expected material envelope to exist");
  return envelope->get();
}

void expectRgbEnvelope(const LX_core::MaterialInstanceSharedPtr &material,
                       const char *parameterName, const LX_core::Vec3f &value) {
  const auto envelope = requireEnvelope(material, parameterName);
  expect(envelope.kind == LX_core::MaterialEnvelopeKind::Rgb,
         "expected rgb envelope kind");
  expect(envelope.rgbValue.has_value(), "expected rgb envelope value");
  expectNear(envelope.rgbValue->x, value.x, "rgb envelope x mismatch");
  expectNear(envelope.rgbValue->y, value.y, "rgb envelope y mismatch");
  expectNear(envelope.rgbValue->z, value.z, "rgb envelope z mismatch");
}

void expectFloatEnvelope(const LX_core::MaterialInstanceSharedPtr &material,
                         const char *parameterName, float value) {
  const auto envelope = requireEnvelope(material, parameterName);
  expect(envelope.kind == LX_core::MaterialEnvelopeKind::Float,
         "expected float envelope kind");
  expect(envelope.floatValue.has_value(), "expected float envelope value");
  expectNear(*envelope.floatValue, value, "float envelope mismatch");
}

void expectStringEnvelope(const LX_core::MaterialInstanceSharedPtr &material,
                          const char *parameterName,
                          const std::string &value) {
  const auto envelope = requireEnvelope(material, parameterName);
  expect(envelope.kind == LX_core::MaterialEnvelopeKind::String,
         "expected string envelope kind");
  expect(envelope.stringValue.has_value(), "expected string envelope value");
  expect(*envelope.stringValue == value, "string envelope mismatch");
}

void expectTextureEnvelope(const LX_core::MaterialInstanceSharedPtr &material,
                           const char *parameterName,
                           const std::string &uri) {
  const auto envelope = requireEnvelope(material, parameterName);
  expect(envelope.kind == LX_core::MaterialEnvelopeKind::Texture,
         "expected texture envelope kind");
  expect(envelope.valueType == LX_core::MaterialEnvelopeValueType::Rgb,
         "expected texture envelope rgb sampled value type");
  expect(envelope.uri.has_value(), "expected texture envelope uri");
  expect(*envelope.uri == uri, "texture envelope uri mismatch");
}

void testGltfLoaderExtractsMetallicRoughnessFactorsAndTextures() {
  const std::filesystem::path gltfPath = writeFullPbrFixture();

  infra::GLTFLoader loader;
  loader.load(gltfPath.string());
  const infra::GLTFPbrMaterial &pbr = loader.getMaterial();

  expectNear(pbr.baseColorFactor.x, 0.2f,
             "glTF baseColorFactor r should be extracted");
  expectNear(pbr.baseColorFactor.y, 0.3f,
             "glTF baseColorFactor g should be extracted");
  expectNear(pbr.baseColorFactor.z, 0.4f,
             "glTF baseColorFactor b should be extracted");
  expectNear(pbr.baseColorFactor.w, 0.5f,
             "glTF baseColorFactor a should be extracted");
  expectNear(pbr.metallicFactor, 0.6f,
             "glTF metallicFactor should be extracted");
  expectNear(pbr.roughnessFactor, 0.7f,
             "glTF roughnessFactor should be extracted");
  expect(pbr.alphaMode == "MASK", "glTF alphaMode should be extracted");
  expectNear(pbr.alphaCutoff, 0.35f,
             "glTF alphaCutoff should be extracted");
  expect(pbr.baseColorTexture == "albedo.png",
         "glTF base color texture URI should be preserved");
  expect(pbr.metallicRoughnessTexture == "metal_rough.png",
         "glTF metallic-roughness texture URI should be preserved");
  expect(pbr.normalTexture == "normal.png",
         "glTF normal texture URI should be preserved");
  expect(pbr.occlusionTexture == "ao.png",
         "glTF occlusion texture URI should be preserved");
  expect(pbr.emissiveTexture == "emissive.png",
         "glTF emissive texture URI should be preserved");
  expectNear(pbr.emissiveFactor.x, 0.8f,
             "glTF emissiveFactor r should be extracted");
  expectNear(pbr.emissiveFactor.y, 0.9f,
             "glTF emissiveFactor g should be extracted");
  expectNear(pbr.emissiveFactor.z, 1.0f,
             "glTF emissiveFactor b should be extracted");
}

void testDamagedHelmetLoadsStandardPbrCleanPath() {
  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  expect(found, "DamagedHelmet asset root must be discoverable");

  const auto result = LX_infra::scene_asset::loadStandardPbrGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf");

  expect(result.mesh != nullptr, "standard-pbr loader should create mesh");
  expect(result.material != nullptr,
         "standard-pbr loader should create material");
  expect(result.material->getBsdfType() == "standard-pbr",
         "standard-pbr loader should not reuse PBRT uber type");
  expect(result.material->getMaterialSourceUri().string() ==
             "assets://shaders/glsl/common/materials/"
             "standard_pbr.contract.glsl",
         "standard-pbr loader should use the standard-pbr contract source");
  expect(result.material->getMaterialSourceUri().string().find(
             "assets/materials/pbr.material") == std::string::npos,
         "standard-pbr loader should not use pbr.material as success path");

  expectRgbEnvelope(result.material, "baseColor",
                    LX_core::Vec3f{1.0f, 1.0f, 1.0f});
  expectFloatEnvelope(result.material, "metallic", 1.0f);
  expectFloatEnvelope(result.material, "roughness", 1.0f);
  expectRgbEnvelope(result.material, "emissive",
                    LX_core::Vec3f{1.0f, 1.0f, 1.0f});
  expectStringEnvelope(result.material, "alphaMode", "OPAQUE");
  expectFloatEnvelope(result.material, "alphaCutoff", 0.5f);
  expectTextureEnvelope(result.material, "baseColorTexture",
                        "Default_albedo.jpg");
  expectTextureEnvelope(result.material, "metallicRoughnessTexture",
                        "Default_metalRoughness.jpg");
  expectTextureEnvelope(result.material, "normalTexture", "Default_normal.jpg");
  expectTextureEnvelope(result.material, "occlusionTexture", "Default_AO.jpg");
  expectTextureEnvelope(result.material, "emissiveTexture",
                        "Default_emissive.jpg");
  expect(hasTexture(result.material, "baseColorTexture"),
         "standard-pbr loader should bind base color texture");
  expect(hasTexture(result.material, "metallicRoughnessTexture"),
         "standard-pbr loader should bind metallic-roughness texture");
  expect(hasTexture(result.material, "normalTexture"),
         "standard-pbr loader should bind normal texture when tangents exist");
  expect(hasTexture(result.material, "occlusionTexture"),
         "standard-pbr loader should bind occlusion texture");
  expect(hasTexture(result.material, "emissiveTexture"),
         "standard-pbr loader should bind emissive texture");
  expect(!hasTexture(result.material, "Kd"),
         "standard-pbr loader should not populate PBRT Kd texture");
  expect(!hasTexture(result.material, "normalmap"),
         "standard-pbr loader should not populate PBRT normalmap texture");
}

void testDamagedHelmetSharedAssetLoadsFullPbrWithoutParameterBuffers() {
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
  expect(result.material->getMaterialSourceUri().string().find(
             "uber.contract.glsl") != std::string::npos,
         "DamagedHelmet generated material should declare explicit "
         "bsdf.source");
  const auto kd = result.material->getMaterialEnvelope(LX_core::StringID("Kd"));
  expect(kd.has_value(), "DamagedHelmet material should retain Kd envelope");
  expect(kd.has_value() &&
             kd->get().kind == LX_core::MaterialEnvelopeKind::Texture,
         "DamagedHelmet Kd should be stored as PBRT texture envelope");
  expect(kd.has_value() &&
             kd->get().valueType == LX_core::MaterialEnvelopeValueType::Rgb,
         "DamagedHelmet Kd texture envelope should retain rgb valueType");

  expect(result.material->getShaderBindingBufferCount() == 0,
         "material v2 should keep envelope storage without parameter buffers");
}

void testGltfTextureContractCheckUsesInstanceReflection() {
  auto material = LX_core::MaterialInstance::create(
      LX_core::MaterialTemplate::create("gltf_contract_test"));

  LX_core::MaterialContractReflection contract;
  contract.sourceUri =
      LX_core::ResourceUri("memory://materials/gltf.contract.glsl");
  contract.declaredType = "uber";
  contract.reflectionHash = "gltf-contract-test";
  contract.storageAbiHash = "gltf-storage-test";
  contract.accessorAbiHash = "gltf-accessor-test";
  contract.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd",
      true,
      {LX_core::MaterialContractParameterKind::Rgb,
       LX_core::MaterialContractParameterKind::Texture}});
  contract.parameters.push_back(LX_core::MaterialContractParameter{
      "roughness",
      false,
      {LX_core::MaterialContractParameterKind::Float}});
  material->setMaterialContractReflection(std::move(contract));

  expect(LX_infra::scene_asset::gltfMaterialAllowsTextureParameter(*material,
                                                                   "Kd"),
         "glTF texture contract check should use instance reflection for Kd");
  expect(!LX_infra::scene_asset::gltfMaterialAllowsTextureParameter(
             *material, "roughness"),
         "glTF texture contract check should reject non-texture parameter "
         "from instance reflection");
  expect(!LX_infra::scene_asset::gltfMaterialAllowsTextureParameter(
             *material, "missing"),
         "glTF texture contract check should reject missing parameter from "
         "instance reflection");
}

void testGltfTextureContractCheckRejectsMissingInstanceReflection() {
  auto material = LX_core::MaterialInstance::create(
      LX_core::MaterialTemplate::create("gltf_contract_missing"));

  try {
    (void)LX_infra::scene_asset::gltfMaterialAllowsTextureParameter(*material,
                                                                    "Kd");
  } catch (const std::runtime_error &error) {
    expect(std::string(error.what()).find("reflected material contract") !=
               std::string::npos,
           "missing instance contract diagnostic should be explicit");
    return;
  }
  expect(false,
         "glTF texture contract check should throw without instance contract");
}

void writeTextFile(const std::filesystem::path &path, const std::string &text) {
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

bool saveSceneThrowsForDeletedExtension(const std::string &expectedDiagnostic) {
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
  expect(loadSceneThrowsFor(deletedMaterialsSchema, "deleted materials schema"),
         "scene document should reject deleted node materials schema");
}

void testSceneDocumentRejectsDeletedProgrammaticExtensionOnSave() {
  expect(saveSceneThrowsForDeletedExtension("deleted material selector field"),
         "scene document save should reject deleted opaque extension fields");
}

} // namespace

int main() {
  testGltfLoaderExtractsMetallicRoughnessFactorsAndTextures();
  testDamagedHelmetLoadsStandardPbrCleanPath();
  testDamagedHelmetSharedAssetLoadsFullPbrWithoutParameterBuffers();
  testGltfTextureContractCheckUsesInstanceReflection();
  testGltfTextureContractCheckRejectsMissingInstanceReflection();
  testSceneDocumentRejectsDeletedProfileMaterialField();
  testSceneDocumentRejectsDeletedOpaqueMaterialField();
  testSceneDocumentRejectsDeletedNodeMaterialsSchema();
  testSceneDocumentRejectsDeletedProgrammaticExtensionOnSave();
  std::cout << "test_gltf_scene_asset_loader passed\n";
  return 0;
}
