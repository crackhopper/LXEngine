#include "infra/offline/offline_scene_loader.hpp"

#include "core/asset/builtin_meshes.hpp"
#include "core/asset/material_instance.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace LX_infra::offline {
namespace {

using LX_core::CameraProjection;
using LX_core::CameraResource;
using LX_core::LightHandle;
using LX_core::MaterialHandle;
using LX_core::MaterialInstanceSharedPtr;
using LX_core::Mat4f;
using LX_core::MeshBufferSharedPtr;
using LX_core::MeshHandle;
using LX_core::ObjectResource;
using LX_core::SceneResourceTable;
using LX_core::ShaderPropertyType;
using LX_core::StringID;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_infra::scene_io::LightKind;
using LX_infra::scene_io::MaterialOverrideState;
using LX_infra::scene_io::SceneNodeDocument;

constexpr const char *kDefaultMaterialUri =
    "assets/materials/blinnphong_default.material";

struct RegisteredMesh final {
  MeshHandle mesh;
  MeshBufferSharedPtr meshBuffer;
  MaterialInstanceSharedPtr defaultMaterial;
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

[[nodiscard]] bool isGltfMeshUri(const std::string &uri) {
  std::string extension = std::filesystem::path(uri).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return extension == ".gltf" || extension == ".glb";
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

[[nodiscard]] bool splitMaterialParameterKey(const std::string &key,
                                             std::string &binding,
                                             std::string &member) {
  const usize dot = key.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size()) {
    return false;
  }
  binding = key.substr(0, dot);
  member = key.substr(dot + 1);
  return true;
}

[[nodiscard]] bool
coerceMaterialParameterValue(const ShaderPropertyType reflectedType,
                             const LX_core::MaterialParameterValue &input,
                             LX_core::MaterialParameterValue &output) {
  output = input;
  if (reflectedType == ShaderPropertyType::Float &&
      input.type == LX_core::MaterialParameterValueType::Int) {
    output.type = LX_core::MaterialParameterValueType::Float;
    output.floatValue = static_cast<float>(input.intValue);
    return true;
  }
  if (reflectedType == ShaderPropertyType::Int &&
      input.type == LX_core::MaterialParameterValueType::Float) {
    output.type = LX_core::MaterialParameterValueType::Int;
    output.intValue = static_cast<i32>(input.floatValue);
    return true;
  }
  if (reflectedType == ShaderPropertyType::Float) {
    return input.type == LX_core::MaterialParameterValueType::Float;
  }
  if (reflectedType == ShaderPropertyType::Int) {
    return input.type == LX_core::MaterialParameterValueType::Int;
  }
  if (reflectedType == ShaderPropertyType::Vec3) {
    return input.type == LX_core::MaterialParameterValueType::Vec3;
  }
  if (reflectedType == ShaderPropertyType::Vec4) {
    return input.type == LX_core::MaterialParameterValueType::Vec4;
  }
  return false;
}

void applyBaseColorIfSupported(const MaterialInstanceSharedPtr &material,
                               const std::optional<Vec3f> &color) {
  if (!material || !color.has_value()) {
    return;
  }
  const StringID materialUbo("MaterialUBO");
  if (const auto member =
          material->findParameterMember(materialUbo, StringID("baseColor"));
      member.has_value() && member->get().type == ShaderPropertyType::Vec3) {
    material->setParameter(materialUbo, StringID("baseColor"), *color);
    material->syncGpuData();
    return;
  }
  if (const auto member = material->findParameterMember(
          materialUbo, StringID("baseColorFactor"));
      member.has_value() && member->get().type == ShaderPropertyType::Vec4) {
    material->setParameter(materialUbo, StringID("baseColorFactor"),
                           Vec4f{color->x, color->y, color->z, 1.0f});
    material->syncGpuData();
  }
}

void applyGenericMaterialOverrides(const MaterialInstanceSharedPtr &material,
                                   const MaterialOverrideState &overrides) {
  if (!material) {
    return;
  }
  for (const auto &[key, value] : overrides.parameters) {
    std::string binding;
    std::string member;
    if (!splitMaterialParameterKey(key, binding, member)) {
      throw std::runtime_error("invalid material override key: " + key);
    }
    const auto reflectedMember =
        material->findParameterMember(StringID(binding), StringID(member));
    if (!reflectedMember.has_value()) {
      throw std::runtime_error("material parameter not found for override: " +
                               key);
    }
    LX_core::MaterialParameterValue coerced;
    if (!coerceMaterialParameterValue(reflectedMember->get().type, value,
                                      coerced)) {
      throw std::runtime_error(
          "material parameter type mismatch for override: " + key);
    }
    material->setParameterValue(StringID(binding), StringID(member), coerced);
  }
  material->syncGpuData();
}

void applyMaterialOverrides(const MaterialInstanceSharedPtr &material,
                            const MaterialOverrideState &overrides) {
  applyBaseColorIfSupported(material, overrides.baseColor);
  applyGenericMaterialOverrides(material, overrides);
}

[[nodiscard]] std::filesystem::path resolveMaterialPath(
    const OfflineAssetResolver &resolver, const std::optional<std::string> &uri,
    std::vector<std::string> &warnings) {
  if (uri.has_value()) {
    const auto materialPath = resolver.resolve(*uri);
    if (std::filesystem::exists(materialPath)) {
      return materialPath;
    }
    warnings.push_back("material asset not found, using fallback material: " +
                       *uri);
  }
  return resolver.resolve(kDefaultMaterialUri);
}

[[nodiscard]] MaterialInstanceSharedPtr loadMaterialInstance(
    const OfflineAssetResolver &resolver, const std::optional<std::string> &uri,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    std::unordered_map<std::string, MaterialInstanceSharedPtr> &materialCache,
    std::vector<std::string> &warnings) {
  const auto materialPath = resolveMaterialPath(resolver, uri, warnings);
  const std::string cacheKey = materialPath.lexically_normal().string();

  MaterialInstanceSharedPtr prototype;
  if (const auto it = materialCache.find(cacheKey); it != materialCache.end()) {
    prototype = it->second;
  } else {
    prototype = LX_infra::loadGenericMaterial(materialPath);
    materialCache.emplace(cacheKey, prototype);
  }

  auto material = prototype->cloneInstanceData();
  applyMaterialOverrides(material, materialOverrides);
  applyMaterialOverrides(material, nodeOverrides);
  return material;
}

[[nodiscard]] CameraResource makeCameraResource(const SceneNodeDocument &node,
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
      .pose = pose,
      .projection = projection,
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

RegisteredMesh registerMeshUri(
    SceneResourceTable &table, const OfflineAssetResolver &resolver,
    const std::string &meshUri, const bool loadDefaultMaterial,
    std::unordered_map<std::string, RegisteredMesh> &meshByUri) {
  if (const auto it = meshByUri.find(meshUri); it != meshByUri.end()) {
    if (loadDefaultMaterial && !it->second.defaultMaterial &&
        isGltfMeshUri(meshUri)) {
      auto asset =
          LX_infra::scene_asset::loadGltfSceneAsset(resolver.resolve(meshUri));
      it->second.defaultMaterial = std::move(asset.material);
    }
    return it->second;
  }

  MeshBufferSharedPtr mesh;
  MaterialInstanceSharedPtr defaultMaterial;
  if (LX_core::isBuiltinPrimitiveMeshUri(meshUri)) {
    mesh = LX_core::buildBuiltinPrimitiveMesh(meshUri);
  } else if (isGltfMeshUri(meshUri)) {
    if (loadDefaultMaterial) {
      auto asset =
          LX_infra::scene_asset::loadGltfSceneAsset(resolver.resolve(meshUri));
      mesh = std::move(asset.mesh);
      defaultMaterial = std::move(asset.material);
    } else {
      auto asset =
          LX_infra::scene_asset::loadGltfMeshAsset(resolver.resolve(meshUri));
      mesh = std::move(asset.mesh);
    }
  } else {
    throw std::runtime_error("offline scene loader only supports shared "
                             "builtin primitive meshes and glTF meshes: " +
                             meshUri);
  }

  const auto geometryStorageHandle =
      table.registerGeometryStorage(mesh->getGeometryStorage());
  (void)geometryStorageHandle;
  RegisteredMesh registered{
      .mesh = table.registerMesh(mesh),
      .meshBuffer = std::move(mesh),
      .defaultMaterial = std::move(defaultMaterial),
  };
  meshByUri.emplace(meshUri, registered);
  return registered;
}

struct LoadState final {
  OfflineLoadedScene loaded;
  std::unordered_map<std::string, RegisteredMesh> meshByUri;
  std::unordered_map<std::string, MaterialHandle> materialByKey;
  std::unordered_map<std::string, MaterialInstanceSharedPtr> materialCache;
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
        state.loaded.table.registerCamera(makeCameraResource(node, world));
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
    const RegisteredMesh mesh =
        registerMeshUri(state.loaded.table, resolver, meshUri,
                        !node.materialUri.has_value(), state.meshByUri);

    const std::string materialKey =
        node.materialUri.value_or(mesh.defaultMaterial ? meshUri : "default") +
        "|" + path;
    MaterialHandle materialHandle;
    if (const auto it = state.materialByKey.find(materialKey);
        it != state.materialByKey.end()) {
      materialHandle = it->second;
    } else {
      MaterialInstanceSharedPtr material;
      if (!node.materialUri.has_value() && mesh.defaultMaterial) {
        material = mesh.defaultMaterial->cloneInstanceData();
        applyMaterialOverrides(material, node.materialOverrides);
        applyMaterialOverrides(material, node.nodeMaterialOverrides);
      } else {
        material = loadMaterialInstance(
            resolver, node.materialUri, node.materialOverrides,
            node.nodeMaterialOverrides, state.materialCache,
            state.loaded.warnings);
      }
      materialHandle = state.loaded.table.registerMaterial(std::move(material));
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
