#include "infra/offline/offline_scene_loader.hpp"

#include "core/asset/builtin_meshes.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/math/bounds.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/mesh_loader/obj_mesh_loader.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "infra/scene_asset/scene_material_loader.hpp"

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
using LX_core::BoundingBox;
using LX_core::LightHandle;
using LX_core::Mat4f;
using LX_core::MaterialHandle;
using LX_core::MaterialInstanceSharedPtr;
using LX_core::MeshBuffer;
using LX_core::MeshBufferSharedPtr;
using LX_core::MeshHandle;
using LX_core::ObjectResource;
using LX_core::SceneResourceTable;
using LX_core::StringID;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_core::Vec4i;
using LX_core::VertexBuffer;
using LX_core::VertexPosNormalUvBone;
using LX_infra::scene_io::LightKind;
using LX_infra::scene_io::MaterialBindingDocument;
using LX_infra::scene_io::MaterialOverrideState;
using LX_infra::scene_io::SceneNodeDocument;

struct RegisteredMesh final {
  MeshHandle mesh;
  BoundingBox bounds;
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
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".gltf" || extension == ".glb";
}

[[nodiscard]] bool isObjMeshUri(const std::string &uri) {
  std::string extension = std::filesystem::path(uri).extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".obj";
}

[[nodiscard]] MeshBufferSharedPtr buildMeshFromObj(infra::ObjLoader &loader) {
  const auto &positions = loader.getPositions();
  const auto &normals = loader.getNormals();
  const auto &uvs = loader.getTexCoords();
  const auto &indices = loader.getIndices();
  if (positions.empty() || indices.empty()) {
    throw std::runtime_error("OBJ asset has empty mesh geometry");
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
    vertices.emplace_back(positions[i], normal, uv, fallbackTangent, zeroBones,
                          zeroWeights);
  }

  auto vertexBuffer =
      VertexBuffer<VertexPosNormalUvBone>::create(std::move(vertices));
  auto indexBuffer = LX_core::IndexBuffer::create(std::vector<u32>(indices));
  return MeshBuffer::create(vertexBuffer, indexBuffer, loader.getBounds());
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

[[nodiscard]] std::filesystem::path
resolveMaterialPath(const OfflineAssetResolver &resolver,
                    const std::optional<std::string> &uri) {
  if (!uri.has_value() || uri->empty()) {
    throw std::runtime_error("offline scene material requires explicit uri");
  }
  const auto materialPath = resolver.resolve(*uri);
  if (!std::filesystem::exists(materialPath)) {
    throw std::runtime_error("material asset not found: " + *uri);
  }
  return materialPath;
}

[[nodiscard]] MaterialInstanceSharedPtr loadMaterialInstance(
    const OfflineAssetResolver &resolver, const std::optional<std::string> &uri,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    SceneResourceTable &resourceTable,
    std::unordered_map<std::string, MaterialInstanceSharedPtr> &materialCache) {
  const auto materialPath = resolveMaterialPath(resolver, uri);
  const std::string cacheKey = materialPath.lexically_normal().string();

  MaterialBindingDocument binding;
  binding.uri = cacheKey;
  return LX_infra::scene_asset::loadSceneMaterialBinding({
      .meshUri = std::nullopt,
      .binding = std::move(binding),
      .materialOverrides = materialOverrides,
      .nodeMaterialOverrides = nodeOverrides,
      .resolveAssetPath =
          [](const std::string &assetUri) {
            return std::filesystem::path(assetUri);
          },
      .loadGenericMaterial =
          [&materialCache, &resourceTable](const std::filesystem::path &path) {
            const std::string key = path.lexically_normal().string();
            MaterialInstanceSharedPtr prototype;
            if (const auto it = materialCache.find(key);
                it != materialCache.end()) {
              prototype = it->second;
            } else {
              prototype = LX_infra::loadGenericMaterial(path, resourceTable);
              materialCache.emplace(key, prototype);
            }
            return prototype->cloneInstanceData();
          },
  });
}

[[nodiscard]] CameraResource makeCameraResource(const SceneNodeDocument &node,
                                                const Mat4f &world) {
  const Vec3f eye = transformPoint(world, Vec3f{0.0f, 0.0f, 0.0f});
  const Vec3f forward =
      transformVector(world, Vec3f{0.0f, 0.0f, -1.0f}).normalized();
  const Vec3f up = transformVector(world, Vec3f{0.0f, 1.0f, 0.0f}).normalized();
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
  object.worldBounds = mesh.bounds.transformed(world);
  object.visibilityMask = node.visibilityMask;
  object.debugId = StringID(path);
  object.visible = node.visibilityMask != 0;
  return object;
}

[[nodiscard]] LightHandle
registerDirectionalLight(SceneResourceTable &table,
                         const SceneNodeDocument &node) {
  auto light = std::make_unique<LX_core::DirectionalLight>();
  light->setDirection(node.light->direction.normalized());
  light->setColor(node.light->color);
  light->setIntensity(node.light->intensity);
  light->setShadowStrength(node.light->shadowStrength);
  light->setShadowDistance(node.light->shadowDistance);
  light->setShadowCascadeCount(node.light->shadowCascadeCount);
  return table.registerLight(std::move(light));
}

RegisteredMesh
registerMeshUri(SceneResourceTable &table, const OfflineAssetResolver &resolver,
                const std::string &meshUri,
                std::unordered_map<std::string, RegisteredMesh> &meshByUri) {
  if (const auto it = meshByUri.find(meshUri); it != meshByUri.end()) {
    return it->second;
  }

  MeshBufferSharedPtr mesh;
  if (LX_core::isBuiltinPrimitiveMeshUri(meshUri)) {
    mesh = LX_core::buildBuiltinPrimitiveMesh(meshUri);
  } else if (isGltfMeshUri(meshUri)) {
    auto asset =
        LX_infra::scene_asset::loadGltfMeshAsset(resolver.resolve(meshUri));
    mesh = std::move(asset.mesh);
  } else if (isObjMeshUri(meshUri)) {
    infra::ObjLoader loader;
    loader.load(resolver.resolve(meshUri).string());
    mesh = buildMeshFromObj(loader);
  } else {
    throw std::runtime_error("offline scene loader only supports "
                             "builtin primitive, OBJ, and glTF meshes: " +
                             meshUri);
  }

  RegisteredMesh registered{
      .mesh = table.registerMesh(mesh->cloneUnique()),
      .bounds = mesh->getBounds(),
  };
  meshByUri.emplace(meshUri, registered);
  return registered;
}

struct LoadState final {
  OfflineLoadedScene loaded;
  OfflineShaderProvider offlineShaderProvider;
  LX_core::IShaderSharedPtr providedOfflineShader;
  std::unordered_map<std::string, RegisteredMesh> meshByUri;
  std::unordered_map<std::string, MaterialHandle> materialByKey;
  std::unordered_map<std::string, MaterialInstanceSharedPtr> materialCache;
  bool cameraLoaded = false;
  bool directionalLightLoaded = false;
};

[[nodiscard]] LX_core::IShaderSharedPtr resolveProvidedOfflineShader(
    LoadState &state) {
  if (!state.offlineShaderProvider) {
    return nullptr;
  }
  if (!state.providedOfflineShader) {
    state.providedOfflineShader = state.offlineShaderProvider();
    if (!state.providedOfflineShader) {
      throw std::runtime_error(
          "offline shader provider returned no OfflineRayTrace shader");
    }
  }
  return state.providedOfflineShader;
}

void ensureOfflineRayTracePass(MaterialInstanceSharedPtr &material,
                               LoadState &state) {
  if (!material) {
    return;
  }

  if (material->isPassEnabled(LX_core::Pass_OfflineRayTrace) &&
      material->getPassShader(LX_core::Pass_OfflineRayTrace)) {
    if (!state.loaded.offlineShader) {
      state.loaded.offlineShader =
          material->getPassShader(LX_core::Pass_OfflineRayTrace);
    }
    return;
  }

  const LX_core::IShaderSharedPtr shader = resolveProvidedOfflineShader(state);
  if (!shader) {
    return;
  }

  LX_core::MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram.shaderName = shader->getShaderName();
  passDefinition.shaderProgram.shader = shader;
  passDefinition.renderState = LX_core::RenderState{};

  material->getTemplate()->setPassDefinition(
      LX_core::Pass_OfflineRayTrace, std::move(passDefinition));
  material->getTemplate()->rebuildMaterialInterface();
  material->setPassEnabled(LX_core::Pass_OfflineRayTrace, true);

  if (!state.loaded.offlineShader) {
    state.loaded.offlineShader = shader;
  }
}

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
      const auto lightHandle =
          registerDirectionalLight(state.loaded.table, node);
      (void)lightHandle;
      state.directionalLightLoaded = true;
    } else {
      state.loaded.warnings.push_back(
          "offline MVP ignored unsupported light at " + path);
    }
  }

  if (node.meshUri.has_value()) {
    const std::string &meshUri = *node.meshUri;
    const RegisteredMesh mesh = registerMeshUri(
        state.loaded.table, resolver, meshUri, state.meshByUri);

    const std::string materialKey =
        node.materialUri.value_or("missing") + "|" + path;
    MaterialHandle materialHandle;
    if (const auto it = state.materialByKey.find(materialKey);
        it != state.materialByKey.end()) {
      materialHandle = it->second;
    } else {
      MaterialInstanceSharedPtr material = loadMaterialInstance(
          resolver, node.materialUri, node.materialOverrides,
          node.nodeMaterialOverrides, state.loaded.table, state.materialCache);
      ensureOfflineRayTracePass(material, state);
      if (!state.loaded.offlineShader && material &&
          material->isPassEnabled(LX_core::Pass_OfflineRayTrace)) {
        state.loaded.offlineShader =
            material->getPassShader(LX_core::Pass_OfflineRayTrace);
      }
      materialHandle = state.loaded.table.registerMaterial(
          material->cloneInstanceDataUnique());
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

OfflineSceneLoader::OfflineSceneLoader(
    OfflineAssetResolver resolver, OfflineShaderProvider offlineShaderProvider)
    : m_resolver(std::move(resolver)),
      m_offlineShaderProvider(std::move(offlineShaderProvider)) {}

OfflineLoadedScene
OfflineSceneLoader::load(const LX_infra::scene_io::SceneDocument &document,
                         const std::string &cameraPath) const {
  LoadState state;
  state.offlineShaderProvider = m_offlineShaderProvider;
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
    throw std::runtime_error(
        "offline scene contains no supported mesh instances");
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
