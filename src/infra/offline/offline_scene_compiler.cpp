#include "infra/offline/offline_scene_compiler.hpp"

#include "core/asset/builtin_meshes.hpp"
#include "core/asset/mesh.hpp"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace LX_infra::offline {
namespace {

using LX_core::Mat4f;
using LX_core::MeshBuffer;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::offline::OfflineDirectionalLightIR;
using LX_core::offline::OfflineInstanceIR;
using LX_core::offline::OfflineMaterialIR;
using LX_core::offline::OfflineMeshIR;
using LX_core::offline::OfflineSceneIR;
using LX_core::offline::OfflineVertexIR;
using LX_infra::scene_io::LightKind;
using LX_infra::scene_io::SceneNodeDocument;

[[nodiscard]] std::string displayName(const SceneNodeDocument &node) {
  if (!node.name.empty()) {
    return node.name;
  }
  if (!node.nodeName.empty()) {
    return node.nodeName;
  }
  return "node";
}

[[nodiscard]] std::string joinPath(const std::string &parent,
                                   const SceneNodeDocument &node) {
  const std::string name = displayName(node);
  if (parent.empty() || parent == "/") {
    return "/" + name;
  }
  return parent + "/" + name;
}

[[nodiscard]] Vec3f parseVec3(const YAML::Node &node, Vec3f fallback) {
  if (!node || !node.IsSequence() || node.size() < 3) {
    return fallback;
  }
  return Vec3f{node[0].as<float>(), node[1].as<float>(), node[2].as<float>()};
}

[[nodiscard]] float parseFloat(const YAML::Node &node, float fallback) {
  return node ? node.as<float>() : fallback;
}

void applyMaterialParameterMap(
    OfflineMaterialIR &material,
    const std::unordered_map<std::string, LX_core::MaterialParameterValue>
        &params) {
  if (const auto it = params.find("MaterialUBO.baseColor");
      it != params.end() &&
      it->second.type == LX_core::MaterialParameterValueType::Vec3) {
    material.baseColor =
        Vec3f{it->second.vectorValue.x, it->second.vectorValue.y,
              it->second.vectorValue.z};
  }
  if (const auto it = params.find("MaterialUBO.baseColorFactor");
      it != params.end() &&
      it->second.type == LX_core::MaterialParameterValueType::Vec4) {
    material.baseColor =
        Vec3f{it->second.vectorValue.x, it->second.vectorValue.y,
              it->second.vectorValue.z};
  }
  if (const auto it = params.find("MaterialUBO.metallicFactor");
      it != params.end() &&
      it->second.type == LX_core::MaterialParameterValueType::Float) {
    material.metallic = it->second.floatValue;
  }
  if (const auto it = params.find("MaterialUBO.roughnessFactor");
      it != params.end() &&
      it->second.type == LX_core::MaterialParameterValueType::Float) {
    material.roughness = it->second.floatValue;
  }
}

[[nodiscard]] OfflineMaterialIR loadMaterial(
    const OfflineAssetResolver &resolver, const std::optional<std::string> &uri,
    const LX_infra::scene_io::MaterialOverrideState &nodeOverride,
    std::vector<std::string> &warnings) {
  OfflineMaterialIR material;
  material.name = uri.value_or("default");

  if (uri.has_value()) {
    const auto materialPath = resolver.resolve(*uri);
    if (std::filesystem::exists(materialPath)) {
      const YAML::Node root = YAML::LoadFile(materialPath.string());
      if (const YAML::Node params = root["parameters"]; params) {
        material.baseColor = parseVec3(params["MaterialUBO.baseColor"],
                                       material.baseColor);
        material.baseColor = parseVec3(params["MaterialUBO.baseColorFactor"],
                                       material.baseColor);
        material.metallic =
            parseFloat(params["MaterialUBO.metallicFactor"], material.metallic);
        material.roughness =
            parseFloat(params["MaterialUBO.roughnessFactor"], material.roughness);
      }
      if (const YAML::Node resources = root["resources"]; resources) {
        if (const YAML::Node albedo = resources["albedoMap"]; albedo) {
          const std::string value = albedo.as<std::string>();
          if (value != "white" && value != "black") {
            material.albedoTextureRef = value;
          }
        }
      }
    } else {
      warnings.push_back("material asset not found, using fallback constants: " +
                         *uri);
    }
  }

  if (nodeOverride.baseColor.has_value()) {
    material.baseColor = *nodeOverride.baseColor;
  }
  applyMaterialParameterMap(material, nodeOverride.parameters);
  material.roughness = std::max(0.03f, material.roughness);
  return material;
}

[[nodiscard]] u32 findOffset(const LX_core::VertexLayout &layout,
                             const char *name) {
  for (const auto &item : layout.getItems()) {
    if (item.name == name) {
      return item.offset;
    }
  }
  throw std::runtime_error(std::string("builtin mesh missing vertex attribute: ") +
                           name);
}

template <typename T>
[[nodiscard]] T readAttribute(const std::byte *vertex, u32 offset) {
  T value{};
  std::memcpy(&value, vertex + offset, sizeof(T));
  return value;
}

[[nodiscard]] OfflineMeshIR makeOfflineMeshFromSharedMesh(
    const std::string &uri, const MeshBuffer &source) {
  const auto &vertexBuffer = *source.getVertexBuffer();
  const auto &indexBuffer = *source.getIndexBuffer();
  const auto &layout = vertexBuffer.getLayout();
  const u32 posOffset = findOffset(layout, "inPos");
  const u32 normalOffset = findOffset(layout, "inNormal");
  const u32 uvOffset = findOffset(layout, "inUV");
  const auto *vertexBytes =
      static_cast<const std::byte *>(vertexBuffer.getRawData());

  OfflineMeshIR mesh;
  mesh.name = uri;
  mesh.sourceUri = uri;
  mesh.vertices.reserve(source.getVertexCount());
  for (u32 i = 0; i < source.getVertexCount(); ++i) {
    const std::byte *vertex =
        vertexBytes + static_cast<usize>(source.getVertexOffset() + i) *
                          layout.getStride();
    mesh.vertices.push_back(OfflineVertexIR{
        .position = readAttribute<Vec3f>(vertex, posOffset),
        .normal = readAttribute<Vec3f>(vertex, normalOffset).normalized(),
        .uv = readAttribute<Vec2f>(vertex, uvOffset),
    });
  }

  const auto *indices = static_cast<const u32 *>(indexBuffer.getRawData());
  mesh.indices.reserve(source.getIndexCount());
  const u32 indexEnd = source.getIndexOffset() + source.getIndexCount();
  for (u32 i = source.getIndexOffset(); i < indexEnd; ++i) {
    mesh.indices.push_back(indices[i] - source.getVertexOffset());
  }
  return mesh;
}

[[nodiscard]] OfflineMeshIR makeBuiltinMesh(const std::string &uri) {
  return makeOfflineMeshFromSharedMesh(
      uri, *LX_core::buildBuiltinPrimitiveMesh(uri));
}

void visitNode(const OfflineAssetResolver &resolver, const SceneNodeDocument &node,
               const std::string &parentPath, const Mat4f &parentWorld,
               const std::string &cameraPath, OfflineSceneIR &scene,
               std::unordered_map<std::string, u32> &meshByUri,
               std::unordered_map<std::string, u32> &materialByKey) {
  const std::string path = joinPath(parentPath, node);
  const Mat4f world = parentWorld * node.transform.toMat4();

  if (node.camera.has_value() && (cameraPath.empty() || cameraPath == path)) {
    const Vec3f eye =
        LX_core::offline::transformPoint(world, Vec3f{0.0f, 0.0f, 0.0f});
    const Vec3f forward =
        LX_core::offline::transformVector(world, Vec3f{0.0f, 0.0f, -1.0f})
            .normalized();
    const Vec3f up =
        LX_core::offline::transformVector(world, Vec3f{0.0f, 1.0f, 0.0f})
            .normalized();
    const float halfOrthoHeight =
        std::max(node.camera->orthographicHeight, 0.001f) * 0.5f;
    const float halfOrthoWidth =
        halfOrthoHeight * std::max(node.camera->aspect, 0.001f);
    scene.cameraPath = path;
    scene.camera = LX_core::CameraSnapshot{
        .path = path,
        .pose = LX_core::makeCameraPose(eye, forward, up),
        .projection =
            LX_core::CameraProjection{
                .type = node.camera->type,
                .fovYDegrees = node.camera->fovY,
                .aspect = node.camera->aspect,
                .nearPlane = node.camera->nearPlane,
                .farPlane = node.camera->farPlane,
                .left = -halfOrthoWidth,
                .right = halfOrthoWidth,
                .bottom = -halfOrthoHeight,
                .top = halfOrthoHeight,
            },
        .cullingMask = node.camera->cullingMask,
        .active = true,
    };
  }

  if (node.light.has_value()) {
    if (node.light->kind == LightKind::Directional) {
      scene.directionalLights.push_back(OfflineDirectionalLightIR{
          .path = path,
          .direction = node.light->direction.normalized(),
          .color = node.light->color,
          .intensity = node.light->intensity,
      });
    } else {
      scene.warnings.push_back("offline MVP ignored unsupported light at " + path);
    }
  }

  if (node.meshUri.has_value()) {
    const std::string &meshUri = *node.meshUri;
    if (!LX_core::isBuiltinPrimitiveMeshUri(meshUri)) {
      throw std::runtime_error("offline MVP only supports shared builtin primitive meshes; unsupported mesh at " +
                               path + ": " + meshUri);
    }
    u32 meshIndex = 0;
    const auto meshIt = meshByUri.find(meshUri);
    if (meshIt == meshByUri.end()) {
      meshIndex = static_cast<u32>(scene.meshes.size());
      scene.meshes.push_back(makeBuiltinMesh(meshUri));
      meshByUri.emplace(meshUri, meshIndex);
    } else {
      meshIndex = meshIt->second;
    }

    const std::string materialKey =
        node.materialUri.value_or("default") + "|" + path;
    u32 materialIndex = 0;
    const auto materialIt = materialByKey.find(materialKey);
    if (materialIt == materialByKey.end()) {
      materialIndex = static_cast<u32>(scene.materials.size());
      scene.materials.push_back(loadMaterial(resolver, node.materialUri,
                                             node.nodeMaterialOverrides,
                                             scene.warnings));
      materialByKey.emplace(materialKey, materialIndex);
    } else {
      materialIndex = materialIt->second;
    }

    scene.instances.push_back(OfflineInstanceIR{
        .path = path,
        .meshIndex = meshIndex,
        .materialIndex = materialIndex,
        .worldTransform = world,
        .visible = node.visibilityMask != 0,
    });
  }

  for (const auto &child : node.children) {
    visitNode(resolver, child, path, world, cameraPath, scene, meshByUri,
              materialByKey);
  }
}

} // namespace

OfflineSceneCompiler::OfflineSceneCompiler(OfflineAssetResolver resolver)
    : m_resolver(std::move(resolver)) {}

OfflineSceneIR OfflineSceneCompiler::compile(
    const LX_infra::scene_io::SceneDocument &document,
    const std::string &cameraPath) const {
  OfflineSceneIR scene;
  scene.name = document.sceneName();
  if (document.hasEnvironment()) {
    scene.environment.enabled = document.environment().enabled;
    scene.environment.hdrUri = document.environment().hdrUri;
    scene.environment.intensity = document.environment().intensity;
  }

  std::unordered_map<std::string, u32> meshByUri;
  std::unordered_map<std::string, u32> materialByKey;
  const std::string requestedCamera =
      cameraPath.empty() ? document.gameplayCameraPath() : cameraPath;
  const auto &root = document.rootNode();
  for (const auto &child : root.children) {
    visitNode(m_resolver, child, "", root.transform.toMat4(), requestedCamera,
              scene, meshByUri, materialByKey);
  }

  if (scene.cameraPath.empty()) {
    throw std::runtime_error("offline render camera not found: " +
                             requestedCamera);
  }
  if (scene.instances.empty()) {
    throw std::runtime_error("offline scene contains no supported mesh instances");
  }
  if (scene.directionalLights.empty()) {
    scene.warnings.push_back("offline scene has no directional light; direct light disabled");
  }
  return scene;
}

OfflineSceneIR OfflineSceneCompiler::compileFile(
    const std::filesystem::path &path, const std::string &cameraPath) const {
  const auto document = LX_infra::scene_io::loadSceneDocument(path);
  return compile(document, cameraPath);
}

} // namespace LX_infra::offline
