#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include "core/asset/texture.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/mesh_loader/gltf_mesh_loader.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {
namespace {

using LX_core::CombinedTextureSampler;
using LX_core::CombinedTextureSamplerSharedPtr;
using LX_core::IndexBuffer;
using LX_core::MaterialEnvelopeKind;
using LX_core::MaterialEnvelopeValueType;
using LX_core::MaterialInstanceSharedPtr;
using LX_core::MaterialParameterEnvelope;
using LX_core::MaterialTemplate;
using LX_core::MeshSharedPtr;
using LX_core::ResourceUri;
using LX_core::StringID;
using LX_core::Texture;
using LX_core::TextureDesc;
using LX_core::TextureFormat;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_core::Vec4i;
using LX_core::VertexBuffer;
using LX_core::VertexPosNormalUvBone;

constexpr const char *kStandardPbrType = "standard-pbr";
constexpr const char *kStandardPbrSourceUri =
    "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl";

[[nodiscard]] std::filesystem::path
resolveGltfPath(const std::filesystem::path &gltfPath) {
  return gltfPath.is_absolute() ? gltfPath : ::resolveRuntimePath(gltfPath);
}

[[nodiscard]] std::vector<Vec4f> generateTangents(
    const std::vector<Vec3f> &positions, const std::vector<Vec3f> &normals,
    const std::vector<Vec2f> &uvs, const std::vector<u32> &indices) {
  std::vector<Vec3f> tangentSums(positions.size(), Vec3f{0.0f, 0.0f, 0.0f});
  std::vector<Vec3f> bitangentSums(positions.size(), Vec3f{0.0f, 0.0f, 0.0f});

  for (usize i = 0; i + 2u < indices.size(); i += 3u) {
    const u32 i0 = indices[i + 0u];
    const u32 i1 = indices[i + 1u];
    const u32 i2 = indices[i + 2u];
    if (i0 >= positions.size() || i1 >= positions.size() ||
        i2 >= positions.size() || i0 >= uvs.size() || i1 >= uvs.size() ||
        i2 >= uvs.size()) {
      continue;
    }

    const Vec3f edge1 = positions[i1] - positions[i0];
    const Vec3f edge2 = positions[i2] - positions[i0];
    const Vec2f deltaUv1 = uvs[i1] - uvs[i0];
    const Vec2f deltaUv2 = uvs[i2] - uvs[i0];
    const float determinant = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;
    if (std::abs(determinant) < 1.0e-6f) {
      continue;
    }

    const float r = 1.0f / determinant;
    const Vec3f tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * r;
    const Vec3f bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * r;
    tangentSums[i0] += tangent;
    tangentSums[i1] += tangent;
    tangentSums[i2] += tangent;
    bitangentSums[i0] += bitangent;
    bitangentSums[i1] += bitangent;
    bitangentSums[i2] += bitangent;
  }

  std::vector<Vec4f> generated;
  generated.reserve(positions.size());
  const Vec3f fallbackTangent{1.0f, 0.0f, 0.0f};
  for (usize i = 0; i < positions.size(); ++i) {
    const Vec3f normal =
        i < normals.size() ? normals[i].normalized() : Vec3f{0.0f, 1.0f, 0.0f};
    Vec3f tangent = tangentSums[i] - normal * normal.dot(tangentSums[i]);
    if (tangent.length2() <= 1.0e-8f) {
      tangent = fallbackTangent;
    } else {
      tangent = tangent.normalized();
    }
    const float sign =
        normal.cross(tangent).dot(bitangentSums[i]) < 0.0f ? -1.0f : 1.0f;
    generated.emplace_back(tangent.x, tangent.y, tangent.z, sign);
  }
  return generated;
}

[[nodiscard]] CombinedTextureSamplerSharedPtr
loadCombinedTexture(const std::filesystem::path &path) {
  infra::TextureLoader loader;
  loader.load(path.string());
  if (loader.getWidth() <= 0 || loader.getHeight() <= 0 ||
      loader.getData() == nullptr) {
    throw std::runtime_error("failed to load texture: " + path.string());
  }

  const usize byteCount = static_cast<usize>(loader.getWidth()) *
                          static_cast<usize>(loader.getHeight()) * 4u;
  std::vector<u8> pixels(loader.getData(), loader.getData() + byteCount);
  TextureDesc desc{static_cast<u32>(loader.getWidth()),
                   static_cast<u32>(loader.getHeight()), TextureFormat::RGBA8};
  auto texture = std::make_shared<Texture>(desc, std::move(pixels));
  return std::make_shared<CombinedTextureSampler>(std::move(texture));
}

[[nodiscard]] GltfMeshAssetLoadResult
buildMeshFromGltf(const infra::GLTFLoader &loader) {
  const auto &positions = loader.getPositions();
  const auto &normals = loader.getNormals();
  const auto &uvs = loader.getTexCoords();
  const auto &authoredTangents = loader.getTangents();
  const auto &indices = loader.getIndices();
  if (positions.empty() || indices.empty()) {
    throw std::runtime_error("glTF asset has empty mesh geometry");
  }

  GltfMeshAssetLoadResult result;
  if (normals.empty()) {
    result.warnings.push_back("glTF has no NORMAL stream; using {0,1,0}");
  }
  if (uvs.empty()) {
    result.warnings.push_back("glTF has no TEXCOORD_0 stream; using {0,0}");
  }

  std::vector<Vec4f> generated;
  if (authoredTangents.empty() && !uvs.empty()) {
    generated = generateTangents(positions, normals, uvs, indices);
    result.generatedTangents = !generated.empty();
    if (result.generatedTangents) {
      result.warnings.push_back(
          "glTF has no TANGENT stream; generated tangents");
    }
  } else if (authoredTangents.empty()) {
    result.warnings.push_back(
        "glTF has no TANGENT stream and no UVs; using fallback tangent");
  }

  std::vector<VertexPosNormalUvBone> vertices;
  vertices.reserve(positions.size());
  const Vec3f fallbackNormal{0.0f, 1.0f, 0.0f};
  const Vec2f fallbackUv{0.0f, 0.0f};
  const Vec4f fallbackTangent{1.0f, 0.0f, 0.0f, 1.0f};
  const Vec4i zeroBones{0, 0, 0, 0};
  const Vec4f zeroWeights{0.0f, 0.0f, 0.0f, 0.0f};

  for (usize i = 0; i < positions.size(); ++i) {
    const Vec3f normal = i < normals.size() ? normals[i] : fallbackNormal;
    const Vec2f uv = i < uvs.size() ? uvs[i] : fallbackUv;
    const Vec4f tangent =
        i < authoredTangents.size()
            ? authoredTangents[i]
            : (i < generated.size() ? generated[i] : fallbackTangent);
    vertices.emplace_back(positions[i], normal, uv, tangent, zeroBones,
                          zeroWeights);
  }

  auto vertexBuffer =
      VertexBuffer<VertexPosNormalUvBone>::create(std::move(vertices));
  auto indexBuffer = IndexBuffer::create(std::vector<u32>(indices));
  result.mesh =
      LX_core::Mesh::create(vertexBuffer, indexBuffer, loader.getBounds());
  return result;
}

void bindV2TextureEnvelopeIfPresent(MaterialInstanceSharedPtr &material,
                                    const std::filesystem::path &gltfDir,
                                    const std::string &uri,
                                    const char *parameterName,
                                    MaterialEnvelopeValueType valueType) {
  if (uri.empty()) {
    return;
  }
  if (!gltfMaterialAllowsTextureParameter(*material, parameterName)) {
    return;
  }

  const auto existing = material->getMaterialEnvelope(StringID(parameterName));
  MaterialParameterEnvelope envelope;
  if (existing.has_value()) {
    envelope = existing->get();
  }
  envelope.kind = MaterialEnvelopeKind::Texture;
  envelope.valueType = valueType;
  envelope.uri = uri;
  envelope.floatValue.reset();
  envelope.rgbValue.reset();
  envelope.boolValue.reset();
  envelope.stringValue.reset();
  envelope.integerValue.reset();
  material->setMaterialEnvelope(StringID(parameterName), std::move(envelope));
  material->setTexture(StringID(parameterName),
                       loadCombinedTexture(gltfDir / uri));
}

void setRgbEnvelope(MaterialInstanceSharedPtr &material,
                    const char *parameterName, Vec3f value) {
  MaterialParameterEnvelope envelope;
  envelope.kind = MaterialEnvelopeKind::Rgb;
  envelope.rgbValue = value;
  material->setMaterialEnvelope(StringID(parameterName), std::move(envelope));
}

void setFloatEnvelope(MaterialInstanceSharedPtr &material,
                      const char *parameterName, float value) {
  MaterialParameterEnvelope envelope;
  envelope.kind = MaterialEnvelopeKind::Float;
  envelope.floatValue = value;
  material->setMaterialEnvelope(StringID(parameterName), std::move(envelope));
}

void setStringEnvelope(MaterialInstanceSharedPtr &material,
                       const char *parameterName, std::string value) {
  MaterialParameterEnvelope envelope;
  envelope.kind = MaterialEnvelopeKind::String;
  envelope.stringValue = std::move(value);
  material->setMaterialEnvelope(StringID(parameterName), std::move(envelope));
}

void setTextureEnvelope(MaterialInstanceSharedPtr &material,
                        const std::filesystem::path &gltfDir,
                        const std::string &uri, const char *parameterName,
                        bool loadTexture) {
  if (uri.empty()) {
    return;
  }
  if (!gltfMaterialAllowsTextureParameter(*material, parameterName)) {
    throw std::runtime_error(std::string("standard-pbr contract does not allow "
                                         "texture parameter: ") +
                             parameterName);
  }

  MaterialParameterEnvelope envelope;
  envelope.kind = MaterialEnvelopeKind::Texture;
  envelope.valueType = MaterialEnvelopeValueType::Rgb;
  envelope.uri = uri;
  material->setMaterialEnvelope(StringID(parameterName), std::move(envelope));
  if (loadTexture) {
    material->setTexture(StringID(parameterName),
                         loadCombinedTexture(gltfDir / uri));
  }
}

[[nodiscard]] MaterialInstanceSharedPtr buildStandardPbrMaterialFromGltf(
    const infra::GLTFPbrMaterial &pbr, const std::filesystem::path &gltfDir,
    const bool normalMapEnabled) {
  auto material = LX_core::MaterialInstance::create(
      MaterialTemplate::create(kStandardPbrType));
  material->setBsdfType(kStandardPbrType);

  const ResourceUri sourceUri(kStandardPbrSourceUri);
  const auto reflected = LX_infra::loadAndReflectMaterialContractSource(sourceUri);
  if (!reflected.diagnostics.empty() || !reflected.reflection.has_value()) {
    std::string message = "failed to reflect standard-pbr material contract";
    for (const std::string &diagnostic : reflected.diagnostics) {
      message += "\n  " + diagnostic;
    }
    throw std::runtime_error(message);
  }

  const auto &reflection = *reflected.reflection;
  if (reflection.declaredType != kStandardPbrType) {
    throw std::runtime_error("standard-pbr contract declared unexpected type: " +
                             reflection.declaredType);
  }

  material->setMaterialSourceUri(sourceUri);
  material->setMaterialSourceReflectionHash(reflection.reflectionHash);
  material->setMaterialSourceSignature(reflection.sourceSignature());
  material->setMaterialContractReflection(reflection);

  setRgbEnvelope(material, "baseColor",
                 Vec3f{pbr.baseColorFactor.x, pbr.baseColorFactor.y,
                       pbr.baseColorFactor.z});
  setFloatEnvelope(material, "metallic", pbr.metallicFactor);
  setFloatEnvelope(material, "roughness", pbr.roughnessFactor);
  setRgbEnvelope(material, "emissive", pbr.emissiveFactor);
  setStringEnvelope(material, "alphaMode", pbr.alphaMode);
  setFloatEnvelope(material, "alphaCutoff", pbr.alphaCutoff);

  setTextureEnvelope(material, gltfDir, pbr.baseColorTexture,
                     "baseColorTexture", true);
  setTextureEnvelope(material, gltfDir, pbr.metallicRoughnessTexture,
                     "metallicRoughnessTexture", true);
  setTextureEnvelope(material, gltfDir, pbr.normalTexture, "normalTexture",
                     normalMapEnabled);
  setTextureEnvelope(material, gltfDir, pbr.occlusionTexture,
                     "occlusionTexture", true);
  setTextureEnvelope(material, gltfDir, pbr.emissiveTexture,
                     "emissiveTexture", true);

  material->syncGpuData();
  return material;
}

[[nodiscard]] MaterialInstanceSharedPtr buildMaterialFromGltf(
    const infra::GLTFPbrMaterial &pbr, const std::filesystem::path &gltfDir,
    const std::filesystem::path &materialUri, const bool normalMapEnabled) {
  auto material = LX_infra::loadGenericMaterial(materialUri);
  if (!material) {
    throw std::runtime_error("failed to load glTF target material: " +
                             materialUri.string());
  }

  bindV2TextureEnvelopeIfPresent(
      material, gltfDir, pbr.baseColorTexture, "Kd",
      LX_core::MaterialEnvelopeValueType::Rgb);
  if (normalMapEnabled) {
    bindV2TextureEnvelopeIfPresent(
        material, gltfDir, pbr.normalTexture, "normalmap",
        LX_core::MaterialEnvelopeValueType::Rgb);
  }

  material->syncGpuData();
  return material;
}

} // namespace

bool gltfMaterialAllowsTextureParameter(
    const LX_core::MaterialInstance &material, const char *parameterName) {
  const auto contract = material.getMaterialContractReflection();
  if (!contract.has_value()) {
    throw std::runtime_error(
        "glTF material binding requires reflected material contract");
  }
  const auto parameter = contract->get().findParameter(parameterName);
  if (!parameter.has_value()) {
    return false;
  }
  return std::find(parameter->get().allowedKinds.begin(),
                   parameter->get().allowedKinds.end(),
                   LX_core::MaterialContractParameterKind::Texture) !=
         parameter->get().allowedKinds.end();
}

GltfMeshAssetLoadResult
loadGltfMeshAsset(const std::filesystem::path &gltfPath) {
  const std::filesystem::path resolved = resolveGltfPath(gltfPath);

  infra::GLTFLoader loader;
  loader.load(resolved.string());
  return buildMeshFromGltf(loader);
}

GltfSceneAssetLoadResult
loadGltfSceneAsset(const std::filesystem::path &gltfPath,
                   const std::filesystem::path &pbrMaterialUri) {
  const std::filesystem::path resolved = resolveGltfPath(gltfPath);

  infra::GLTFLoader loader;
  loader.load(resolved.string());

  const GltfMeshAssetLoadResult meshResult = buildMeshFromGltf(loader);

  GltfSceneAssetLoadResult result;
  result.mesh = meshResult.mesh;
  result.generatedTangents = meshResult.generatedTangents;
  result.warnings = meshResult.warnings;

  const bool hasTangentBasis =
      !loader.getTangents().empty() || result.generatedTangents;
  result.normalMapEnabled =
      hasTangentBasis && !loader.getMaterial().normalTexture.empty();
  if (!result.normalMapEnabled && !loader.getMaterial().normalTexture.empty()) {
    result.warnings.push_back(
        "normal map disabled because tangent basis is unavailable");
  }

  result.material =
      buildMaterialFromGltf(loader.getMaterial(), resolved.parent_path(),
                            pbrMaterialUri, result.normalMapEnabled);
  return result;
}

GltfSceneAssetLoadResult
loadStandardPbrGltfSceneAsset(const std::filesystem::path &gltfPath) {
  const std::filesystem::path resolved = resolveGltfPath(gltfPath);

  infra::GLTFLoader loader;
  loader.load(resolved.string());

  const GltfMeshAssetLoadResult meshResult = buildMeshFromGltf(loader);

  GltfSceneAssetLoadResult result;
  result.mesh = meshResult.mesh;
  result.generatedTangents = meshResult.generatedTangents;
  result.warnings = meshResult.warnings;

  const bool hasTangentBasis =
      !loader.getTangents().empty() || result.generatedTangents;
  result.normalMapEnabled =
      hasTangentBasis && !loader.getMaterial().normalTexture.empty();
  if (!result.normalMapEnabled && !loader.getMaterial().normalTexture.empty()) {
    result.warnings.push_back(
        "normal map disabled because tangent basis is unavailable");
  }

  result.material = buildStandardPbrMaterialFromGltf(
      loader.getMaterial(), resolved.parent_path(), result.normalMapEnabled);
  return result;
}

} // namespace LX_infra::scene_asset
