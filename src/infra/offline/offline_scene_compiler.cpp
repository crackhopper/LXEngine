#include "infra/offline/offline_scene_compiler.hpp"

#include "yaml-cpp/yaml.h"

#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace LX_infra::offline {
namespace {

using LX_core::Mat4f;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::offline::OfflineCameraIR;
using LX_core::offline::OfflineDirectionalLightIR;
using LX_core::offline::OfflineInstanceIR;
using LX_core::offline::OfflineMaterialIR;
using LX_core::offline::OfflineMeshIR;
using LX_core::offline::OfflineSceneIR;
using LX_core::offline::OfflineVertexIR;
using LX_infra::scene_io::LightKind;
using LX_infra::scene_io::SceneNodeDocument;

constexpr float kPi = 3.14159265358979323846f;

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
  material.roughness = std::max(0.03f, material.roughness);
  return material;
}

void appendPlane(OfflineMeshIR &mesh) {
  mesh.vertices = {
      {{-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      {{0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
      {{0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
      {{-0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
  };
  mesh.indices = {0, 1, 2, 0, 2, 3};
}

void appendSphere(OfflineMeshIR &mesh) {
  constexpr u32 latSegments = 16;
  constexpr u32 lonSegments = 32;
  for (u32 y = 0; y <= latSegments; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(latSegments);
    const float theta = v * kPi;
    for (u32 x = 0; x <= lonSegments; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(lonSegments);
      const float phi = u * 2.0f * kPi;
      Vec3f normal{std::sin(theta) * std::cos(phi), std::cos(theta),
                   std::sin(theta) * std::sin(phi)};
      mesh.vertices.push_back({normal * 0.5f, normal.normalized(), {u, v}});
    }
  }
  const u32 stride = lonSegments + 1;
  for (u32 y = 0; y < latSegments; ++y) {
    for (u32 x = 0; x < lonSegments; ++x) {
      const u32 i0 = y * stride + x;
      const u32 i1 = i0 + 1;
      const u32 i2 = i0 + stride;
      const u32 i3 = i2 + 1;
      mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
    }
  }
}

[[nodiscard]] OfflineMeshIR makeBuiltinMesh(const std::string &uri) {
  OfflineMeshIR mesh;
  mesh.name = uri;
  mesh.sourceUri = uri;
  if (uri == "builtin://lxe_editor/primitives/plane") {
    appendPlane(mesh);
    return mesh;
  }
  if (uri == "builtin://lxe_editor/primitives/sphere") {
    appendSphere(mesh);
    return mesh;
  }
  throw std::runtime_error("unsupported builtin offline mesh: " + uri);
}

void visitNode(const OfflineAssetResolver &resolver, const SceneNodeDocument &node,
               const std::string &parentPath, const Mat4f &parentWorld,
               const std::string &cameraPath, OfflineSceneIR &scene,
               std::unordered_map<std::string, u32> &meshByUri,
               std::unordered_map<std::string, u32> &materialByKey) {
  const std::string path = joinPath(parentPath, node);
  const Mat4f world = parentWorld * node.transform.toMat4();

  if (node.camera.has_value() && (cameraPath.empty() || cameraPath == path)) {
    scene.cameraPath = path;
    scene.camera.path = path;
    scene.camera.eye = node.camera->eye;
    scene.camera.target = node.camera->target;
    scene.camera.up = node.camera->up;
    scene.camera.fovYDegrees = node.camera->fovY;
    scene.camera.aspect = node.camera->aspect;
    scene.camera.nearPlane = node.camera->nearPlane;
    scene.camera.farPlane = node.camera->farPlane;
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
    if (meshUri.rfind("builtin://lxe_editor/primitives/", 0) != 0) {
      throw std::runtime_error("offline MVP only supports builtin sphere/plane; unsupported mesh at " +
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
