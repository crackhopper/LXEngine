#include "infra/offline/offline_scene_loader.hpp"

#include "core/asset/builtin_meshes.hpp"
#include "core/asset/material_instance.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace LX_infra::offline {
namespace {

using LX_core::BoundingBox;
using LX_core::CameraProjection;
using LX_core::CameraResource;
using LX_core::GeometryStorageHandle;
using LX_core::LightHandle;
using LX_core::MaterialHandle;
using LX_core::MaterialInstance;
using LX_core::MaterialInstanceSharedPtr;
using LX_core::MaterialPassDefinition;
using LX_core::MaterialTemplate;
using LX_core::Mat4f;
using LX_core::MeshBufferSharedPtr;
using LX_core::MeshHandle;
using LX_core::ObjectResource;
using LX_core::Pass_Forward;
using LX_core::RenderState;
using LX_core::SceneResourceTable;
using LX_core::ShaderProgramSet;
using LX_core::ShaderPropertyType;
using LX_core::ShaderResourceBinding;
using LX_core::ShaderStageCode;
using LX_core::StringID;
using LX_core::StructMemberInfo;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_infra::scene_io::LightKind;
using LX_infra::scene_io::MaterialOverrideState;
using LX_infra::scene_io::SceneNodeDocument;

struct MaterialConstants final {
  Vec4f baseColor{0.8f, 0.8f, 0.8f, 1.0f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float specularIntensity = 0.0f;
  float ambientIntensity = 0.0f;
  float shininess = 32.0f;
  Vec3f emissive{0.0f, 0.0f, 0.0f};
};

struct RegisteredMesh final {
  GeometryStorageHandle geometryStorage;
  MeshHandle mesh;
  MeshBufferSharedPtr meshBuffer;
};

class OfflineMaterialShader final : public LX_core::IShader {
public:
  explicit OfflineMaterialShader(std::vector<ShaderResourceBinding> bindings)
      : m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &item : m_bindings) {
      if (item.set == set && item.binding == binding) {
        return std::cref(item);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &item : m_bindings) {
      if (item.name == name) {
        return std::cref(item);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 0; }
  std::string getShaderName() const override { return "offline_scene"; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

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

[[nodiscard]] Vec3f transformPoint(const Mat4f &matrix, const Vec3f &point) {
  const Vec4f transformed = matrix * Vec4f{point.x, point.y, point.z, 1.0f};
  if (std::abs(transformed.w) > 1.0e-8f) {
    return {transformed.x / transformed.w, transformed.y / transformed.w,
            transformed.z / transformed.w};
  }
  return {transformed.x, transformed.y, transformed.z};
}

[[nodiscard]] Vec3f transformVector(const Mat4f &matrix, const Vec3f &vector) {
  const Vec4f transformed = matrix * Vec4f{vector.x, vector.y, vector.z, 0.0f};
  return {transformed.x, transformed.y, transformed.z};
}

[[nodiscard]] Mat4f inverseAffine(const Mat4f &m) {
  const f32 a00 = m(0, 0);
  const f32 a01 = m(0, 1);
  const f32 a02 = m(0, 2);
  const f32 a10 = m(1, 0);
  const f32 a11 = m(1, 1);
  const f32 a12 = m(1, 2);
  const f32 a20 = m(2, 0);
  const f32 a21 = m(2, 1);
  const f32 a22 = m(2, 2);

  const f32 c00 = a11 * a22 - a12 * a21;
  const f32 c01 = -(a10 * a22 - a12 * a20);
  const f32 c02 = a10 * a21 - a11 * a20;
  const f32 c10 = -(a01 * a22 - a02 * a21);
  const f32 c11 = a00 * a22 - a02 * a20;
  const f32 c12 = -(a00 * a21 - a01 * a20);
  const f32 c20 = a01 * a12 - a02 * a11;
  const f32 c21 = -(a00 * a12 - a02 * a10);
  const f32 c22 = a00 * a11 - a01 * a10;
  const f32 det = a00 * c00 + a01 * c01 + a02 * c02;
  if (std::abs(det) < 1.0e-8f) {
    return Mat4f::identity();
  }
  const f32 invDet = 1.0f / det;

  Mat4f inverse = Mat4f::identity();
  inverse(0, 0) = c00 * invDet;
  inverse(0, 1) = c10 * invDet;
  inverse(0, 2) = c20 * invDet;
  inverse(1, 0) = c01 * invDet;
  inverse(1, 1) = c11 * invDet;
  inverse(1, 2) = c21 * invDet;
  inverse(2, 0) = c02 * invDet;
  inverse(2, 1) = c12 * invDet;
  inverse(2, 2) = c22 * invDet;

  const Vec3f t{m(0, 3), m(1, 3), m(2, 3)};
  inverse(0, 3) =
      -(inverse(0, 0) * t.x + inverse(0, 1) * t.y + inverse(0, 2) * t.z);
  inverse(1, 3) =
      -(inverse(1, 0) * t.x + inverse(1, 1) * t.y + inverse(1, 2) * t.z);
  inverse(2, 3) =
      -(inverse(2, 0) * t.x + inverse(2, 1) * t.y + inverse(2, 2) * t.z);
  return inverse;
}

[[nodiscard]] Vec3f parseVec3(const YAML::Node &node, Vec3f fallback) {
  if (!node || !node.IsSequence() || node.size() < 3) {
    return fallback;
  }
  return Vec3f{node[0].as<float>(), node[1].as<float>(), node[2].as<float>()};
}

[[nodiscard]] Vec4f parseVec4(const YAML::Node &node, Vec4f fallback) {
  if (!node || !node.IsSequence() || node.size() < 3) {
    return fallback;
  }
  return Vec4f{node[0].as<float>(), node[1].as<float>(),
               node[2].as<float>(),
               node.size() >= 4 ? node[3].as<float>() : fallback.w};
}

[[nodiscard]] float parseFloat(const YAML::Node &node, float fallback) {
  return node ? node.as<float>() : fallback;
}

void applyMaterialParameter(MaterialConstants &material,
                            const std::string &key,
                            const LX_core::MaterialParameterValue &value) {
  if ((key == "MaterialUBO.baseColor" ||
       key == "MaterialUBO.surfaceColor") &&
      value.type == LX_core::MaterialParameterValueType::Vec3) {
    material.baseColor.x = value.vectorValue.x;
    material.baseColor.y = value.vectorValue.y;
    material.baseColor.z = value.vectorValue.z;
  } else if (key == "MaterialUBO.baseColorFactor" &&
             value.type == LX_core::MaterialParameterValueType::Vec4) {
    material.baseColor = value.vectorValue;
  } else if ((key == "MaterialUBO.metallicFactor" ||
              key == "MaterialUBO.metallic") &&
             value.type == LX_core::MaterialParameterValueType::Float) {
    material.metallic = value.floatValue;
  } else if ((key == "MaterialUBO.roughnessFactor" ||
              key == "MaterialUBO.roughness") &&
             value.type == LX_core::MaterialParameterValueType::Float) {
    material.roughness = value.floatValue;
  } else if (key == "MaterialUBO.specularIntensity" &&
             value.type == LX_core::MaterialParameterValueType::Float) {
    material.specularIntensity = value.floatValue;
  } else if ((key == "MaterialUBO.ambientIntensity" ||
              key == "MaterialUBO.ao") &&
             value.type == LX_core::MaterialParameterValueType::Float) {
    material.ambientIntensity = value.floatValue;
  } else if (key == "MaterialUBO.shininess" &&
             value.type == LX_core::MaterialParameterValueType::Float) {
    material.shininess = value.floatValue;
  } else if ((key == "MaterialUBO.emissive" ||
              key == "MaterialUBO.emissiveFactor") &&
             value.type == LX_core::MaterialParameterValueType::Vec3) {
    material.emissive = {value.vectorValue.x, value.vectorValue.y,
                         value.vectorValue.z};
  }
}

void applyMaterialParameterMap(
    MaterialConstants &material,
    const std::unordered_map<std::string, LX_core::MaterialParameterValue>
        &params) {
  for (const auto &[key, value] : params) {
    applyMaterialParameter(material, key, value);
  }
}

void applyMaterialOverrides(MaterialConstants &material,
                            const MaterialOverrideState &overrides) {
  if (overrides.baseColor.has_value()) {
    material.baseColor.x = overrides.baseColor->x;
    material.baseColor.y = overrides.baseColor->y;
    material.baseColor.z = overrides.baseColor->z;
  }
  applyMaterialParameterMap(material, overrides.parameters);
}

[[nodiscard]] MaterialConstants
loadMaterialConstants(const OfflineAssetResolver &resolver,
                      const std::optional<std::string> &uri,
                      const MaterialOverrideState &materialOverrides,
                      const MaterialOverrideState &nodeOverrides,
                      std::vector<std::string> &warnings) {
  MaterialConstants material;

  if (uri.has_value()) {
    const auto materialPath = resolver.resolve(*uri);
    if (std::filesystem::exists(materialPath)) {
      const YAML::Node root = YAML::LoadFile(materialPath.string());
      if (const YAML::Node params = root["parameters"]; params) {
        material.baseColor =
            parseVec4(params["MaterialUBO.baseColor"], material.baseColor);
        material.baseColor = parseVec4(params["MaterialUBO.baseColorFactor"],
                                       material.baseColor);
        material.baseColor =
            parseVec4(params["MaterialUBO.surfaceColor"], material.baseColor);
        material.metallic =
            parseFloat(params["MaterialUBO.metallicFactor"], material.metallic);
        material.metallic =
            parseFloat(params["MaterialUBO.metallic"], material.metallic);
        material.roughness = parseFloat(params["MaterialUBO.roughnessFactor"],
                                        material.roughness);
        material.roughness =
            parseFloat(params["MaterialUBO.roughness"], material.roughness);
        material.specularIntensity = parseFloat(
            params["MaterialUBO.specularIntensity"], material.specularIntensity);
        material.ambientIntensity = parseFloat(
            params["MaterialUBO.ambientIntensity"], material.ambientIntensity);
        material.ambientIntensity =
            parseFloat(params["MaterialUBO.ao"], material.ambientIntensity);
        material.shininess =
            parseFloat(params["MaterialUBO.shininess"], material.shininess);
        material.emissive =
            parseVec3(params["MaterialUBO.emissive"], material.emissive);
        material.emissive = parseVec3(params["MaterialUBO.emissiveFactor"],
                                      material.emissive);
      }
    } else {
      warnings.push_back("material asset not found, using fallback constants: " +
                         *uri);
    }
  }

  applyMaterialOverrides(material, materialOverrides);
  applyMaterialOverrides(material, nodeOverrides);
  material.roughness = std::max(0.03f, material.roughness);
  return material;
}

[[nodiscard]] std::vector<StructMemberInfo> makeMaterialUboMembers() {
  return {
      {"baseColorFactor", ShaderPropertyType::Vec4, 0, 16},
      {"baseColor", ShaderPropertyType::Vec3, 0, 12},
      {"surfaceColor", ShaderPropertyType::Vec3, 0, 12},
      {"metallicFactor", ShaderPropertyType::Float, 16, 4},
      {"metallic", ShaderPropertyType::Float, 16, 4},
      {"roughnessFactor", ShaderPropertyType::Float, 20, 4},
      {"roughness", ShaderPropertyType::Float, 20, 4},
      {"specularIntensity", ShaderPropertyType::Float, 24, 4},
      {"ambientIntensity", ShaderPropertyType::Float, 28, 4},
      {"ao", ShaderPropertyType::Float, 28, 4},
      {"shininess", ShaderPropertyType::Float, 32, 4},
      {"emissive", ShaderPropertyType::Vec3, 36, 12},
      {"emissiveFactor", ShaderPropertyType::Vec3, 36, 12},
  };
}

[[nodiscard]] MaterialTemplate::SharedPtr makeOfflineMaterialTemplate() {
  ShaderResourceBinding binding;
  binding.name = "MaterialUBO";
  binding.set = 2;
  binding.binding = 0;
  binding.type = ShaderPropertyType::UniformBuffer;
  binding.size = 48;
  binding.members = makeMaterialUboMembers();

  auto shader = std::make_shared<OfflineMaterialShader>(
      std::vector<ShaderResourceBinding>{binding});
  auto materialTemplate = MaterialTemplate::create("offline_scene");
  ShaderProgramSet shaderSet;
  shaderSet.shaderName = "offline_scene";
  shaderSet.shader = std::move(shader);
  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderSet);
  passDefinition.renderState = RenderState{};
  materialTemplate->setPassDefinition(Pass_Forward, std::move(passDefinition));
  materialTemplate->rebuildMaterialInterface();
  return materialTemplate;
}

[[nodiscard]] MaterialInstanceSharedPtr
makeMaterialInstance(const MaterialConstants &constants) {
  auto material = MaterialInstance::create(makeOfflineMaterialTemplate());
  material->setParameter(StringID("MaterialUBO"), StringID("baseColorFactor"),
                         constants.baseColor);
  material->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                         Vec3f{constants.baseColor.x, constants.baseColor.y,
                               constants.baseColor.z});
  material->setParameter(StringID("MaterialUBO"), StringID("surfaceColor"),
                         Vec3f{constants.baseColor.x, constants.baseColor.y,
                               constants.baseColor.z});
  material->setParameter(StringID("MaterialUBO"), StringID("metallicFactor"),
                         constants.metallic);
  material->setParameter(StringID("MaterialUBO"), StringID("metallic"),
                         constants.metallic);
  material->setParameter(StringID("MaterialUBO"), StringID("roughnessFactor"),
                         constants.roughness);
  material->setParameter(StringID("MaterialUBO"), StringID("roughness"),
                         constants.roughness);
  material->setParameter(StringID("MaterialUBO"),
                         StringID("specularIntensity"),
                         constants.specularIntensity);
  material->setParameter(StringID("MaterialUBO"),
                         StringID("ambientIntensity"),
                         constants.ambientIntensity);
  material->setParameter(StringID("MaterialUBO"), StringID("ao"),
                         constants.ambientIntensity);
  material->setParameter(StringID("MaterialUBO"), StringID("shininess"),
                         constants.shininess);
  material->setParameter(StringID("MaterialUBO"), StringID("emissive"),
                         constants.emissive);
  material->setParameter(StringID("MaterialUBO"), StringID("emissiveFactor"),
                         constants.emissive);
  material->syncGpuData();
  return material;
}

[[nodiscard]] CameraResource makeCameraResource(const SceneNodeDocument &node,
                                                const std::string &path,
                                                const Mat4f &world) {
  const Vec3f eye = transformPoint(world, Vec3f{0.0f, 0.0f, 0.0f});
  const Vec3f forward =
      transformVector(world, Vec3f{0.0f, 0.0f, -1.0f}).normalized();
  const Vec3f up =
      transformVector(world, Vec3f{0.0f, 1.0f, 0.0f}).normalized();
  const float halfOrthoHeight =
      std::max(node.camera->orthographicHeight, 0.001f) * 0.5f;
  const float halfOrthoWidth =
      halfOrthoHeight * std::max(node.camera->aspect, 0.001f);
  const auto pose = LX_core::makeCameraPose(eye, forward, up);
  const CameraProjection projection{
      .type = node.camera->type,
      .fovYDegrees = node.camera->fovY,
      .aspect = node.camera->aspect,
      .nearPlane = node.camera->nearPlane,
      .farPlane = node.camera->farPlane,
      .left = -halfOrthoWidth,
      .right = halfOrthoWidth,
      .bottom = -halfOrthoHeight,
      .top = halfOrthoHeight,
  };
  return CameraResource{
      .view = LX_core::makeCameraViewMatrix(pose),
      .proj = LX_core::makeCameraProjectionMatrix(projection),
      .cullingMask = node.camera->cullingMask,
      .active = true,
  };
}

[[nodiscard]] ObjectResource makeObjectResource(const std::string &path,
                                                const RegisteredMesh &mesh,
                                                MaterialHandle material,
                                                const SceneNodeDocument &node,
                                                const Mat4f &world) {
  ObjectResource object;
  object.mesh = mesh.mesh;
  object.material = material;
  object.objectToWorld = world;
  object.worldToObject = inverseAffine(world);
  object.worldBounds = mesh.meshBuffer->getBounds().transformed(world);
  object.visibilityMask = node.visibilityMask;
  object.debugId = StringID(path);
  object.visible = node.visibilityMask != 0;
  return object;
}

[[nodiscard]] LightHandle registerDirectionalLight(SceneResourceTable &table,
                                                   const SceneNodeDocument &node) {
  auto light = std::make_shared<LX_core::DirectionalLight>();
  light->setDirection(node.light->direction.normalized());
  light->setColor(node.light->color);
  light->setIntensity(node.light->intensity);
  light->setShadowStrength(node.light->shadowStrength);
  light->setShadowDistance(node.light->shadowDistance);
  light->setShadowCascadeCount(node.light->shadowCascadeCount);
  return table.registerLight(std::move(light));
}

RegisteredMesh registerBuiltinMesh(
    SceneResourceTable &table, const std::string &meshUri,
    std::unordered_map<std::string, RegisteredMesh> &meshByUri) {
  if (const auto it = meshByUri.find(meshUri); it != meshByUri.end()) {
    return it->second;
  }

  auto mesh = LX_core::buildBuiltinPrimitiveMesh(meshUri);
  RegisteredMesh registered{
      .geometryStorage = table.registerGeometryStorage(mesh->getGeometryStorage()),
      .mesh = table.registerMesh(mesh),
      .meshBuffer = std::move(mesh),
  };
  meshByUri.emplace(meshUri, registered);
  return registered;
}

struct LoadState final {
  OfflineLoadedScene loaded;
  std::unordered_map<std::string, RegisteredMesh> meshByUri;
  std::unordered_map<std::string, MaterialHandle> materialByKey;
  bool cameraLoaded = false;
  bool directionalLightLoaded = false;
};

void visitNode(const OfflineAssetResolver &resolver,
               const SceneNodeDocument &node, const std::string &parentPath,
               const Mat4f &parentWorld, const std::string &cameraPath,
               LoadState &state) {
  const std::string path = joinPath(parentPath, node);
  const Mat4f world = parentWorld * node.transform.toMat4();

  if (node.camera.has_value() && !state.cameraLoaded &&
      (cameraPath.empty() || cameraPath == path)) {
    const auto cameraHandle =
        state.loaded.table.registerCamera(makeCameraResource(node, path, world));
    (void)cameraHandle;
    state.cameraLoaded = true;
  }

  if (node.light.has_value()) {
    if (node.light->kind == LightKind::Directional) {
      const auto lightHandle = registerDirectionalLight(state.loaded.table, node);
      (void)lightHandle;
      state.directionalLightLoaded = true;
    } else {
      state.loaded.warnings.push_back(
          "offline MVP ignored unsupported light at " + path);
    }
  }

  if (node.meshUri.has_value()) {
    const std::string &meshUri = *node.meshUri;
    if (!LX_core::isBuiltinPrimitiveMeshUri(meshUri)) {
      throw std::runtime_error("offline MVP only supports shared builtin "
                               "primitive meshes; unsupported mesh at " +
                               path + ": " + meshUri);
    }
    const RegisteredMesh mesh =
        registerBuiltinMesh(state.loaded.table, meshUri, state.meshByUri);

    const std::string materialKey =
        node.materialUri.value_or("default") + "|" + path;
    MaterialHandle materialHandle;
    if (const auto it = state.materialByKey.find(materialKey);
        it != state.materialByKey.end()) {
      materialHandle = it->second;
    } else {
      const MaterialConstants material = loadMaterialConstants(
          resolver, node.materialUri, node.materialOverrides,
          node.nodeMaterialOverrides, state.loaded.warnings);
      materialHandle =
          state.loaded.table.registerMaterial(makeMaterialInstance(material));
      state.materialByKey.emplace(materialKey, materialHandle);
    }

    const auto objectHandle = state.loaded.table.registerObject(
        makeObjectResource(path, mesh, materialHandle, node, world));
    (void)objectHandle;
  }

  for (const auto &child : node.children) {
    visitNode(resolver, child, path, world, cameraPath, state);
  }
}

} // namespace

OfflineSceneLoader::OfflineSceneLoader(OfflineAssetResolver resolver)
    : m_resolver(std::move(resolver)) {}

OfflineLoadedScene OfflineSceneLoader::load(
    const LX_infra::scene_io::SceneDocument &document,
    const std::string &cameraPath) const {
  LoadState state;
  const std::string requestedCamera =
      cameraPath.empty() ? document.gameplayCameraPath() : cameraPath;

  const auto &root = document.rootNode();
  for (const auto &child : root.children) {
    visitNode(m_resolver, child, "", root.transform.toMat4(), requestedCamera,
              state);
  }

  if (!state.cameraLoaded) {
    throw std::runtime_error("offline render camera not found: " +
                             requestedCamera);
  }
  if (state.loaded.table.objectCount() == 0) {
    throw std::runtime_error("offline scene contains no supported mesh instances");
  }
  if (!state.directionalLightLoaded) {
    state.loaded.warnings.push_back(
        "offline scene has no directional light; direct light disabled");
  }
  return std::move(state.loaded);
}

OfflineLoadedScene
OfflineSceneLoader::loadFile(const std::filesystem::path &path,
                             const std::string &cameraPath) const {
  const auto document = LX_infra::scene_io::loadSceneDocument(path);
  return load(document, cameraPath);
}

} // namespace LX_infra::offline
