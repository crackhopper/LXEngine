#include "scene_builder.hpp"

#include "core/asset/builtin_meshes.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/texture.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "infra/scene_asset/scene_mesh_loader.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace LX_demo::lxe_editor {

namespace {

using LX_core::CombinedTextureSampler;
using LX_core::CombinedTextureSamplerSharedPtr;
using LX_core::IndexBuffer;
using LX_core::MaterialInstanceSharedPtr;
using LX_core::Mesh;
using LX_core::MeshSharedPtr;
using LX_core::SceneNode;
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

// Load an image file and wrap it in a CombinedTextureSampler the material
// system understands. Uses RGBA8 (stb_image always delivers 4 channels via
// STBI_rgb_alpha, which is what TextureLoader requests internally).
CombinedTextureSamplerSharedPtr
loadCombinedTexture(const std::filesystem::path &path) {
  infra::TextureLoader loader;
  loader.load(path.string());
  const int w = loader.getWidth();
  const int h = loader.getHeight();
  if (w <= 0 || h <= 0 || loader.getData() == nullptr) {
    throw std::runtime_error("[lxe_editor] failed to load texture: " +
                             path.string());
  }

  const usize byteCount = static_cast<usize>(w) * static_cast<usize>(h) * 4;
  std::vector<u8> pixels(loader.getData(), loader.getData() + byteCount);

  TextureDesc desc{static_cast<u32>(w), static_cast<u32>(h),
                   TextureFormat::RGBA8};
  auto tex = std::make_shared<Texture>(desc, std::move(pixels));
  return std::make_shared<CombinedTextureSampler>(std::move(tex));
}

MaterialInstanceSharedPtr makeGroundMaterial() {
  auto mat = LX_infra::loadGenericMaterial("assets/materials/pbr.material");
  if (!mat) {
    throw std::runtime_error(
        "[lxe_editor] failed to load assets/materials/pbr.material");
  }
  mat->syncGpuData();
  return mat;
}

MaterialInstanceSharedPtr makePrimitiveMaterial() {
  auto mat = LX_infra::loadGenericMaterial("assets/materials/pbr.material");
  if (!mat) {
    throw std::runtime_error(
        "[lxe_editor] failed to load assets/materials/pbr.material");
  }
  mat->syncGpuData();
  return mat;
}

MaterialInstanceSharedPtr makeModelMaterial(std::string_view materialUri,
                                            std::string_view albedoTextureUri) {
  constexpr const char *kFallbackMaterial = "assets/materials/pbr.material";

  std::string uri =
      materialUri.empty() ? kFallbackMaterial : std::string(materialUri);
  auto mat = LX_infra::loadGenericMaterial(uri);
  if (!mat) {
    throw std::runtime_error("[lxe_editor] failed to load " + uri);
  }
  if (!albedoTextureUri.empty()) {
    try {
      auto sampler = loadCombinedTexture(
          resolveRuntimePath(std::string(albedoTextureUri)));
      mat->setTexture(StringID("albedoMap"), std::move(sampler));
    } catch (const std::exception &e) {
      std::cerr << "[lxe_editor] model albedo texture load failed (" << e.what()
                << "); falling back to flat color\n";
    }
  }
  mat->syncGpuData();
  return mat;
}

MeshSharedPtr loadModelMesh(std::string_view meshUri) {
  const std::filesystem::path path = resolveRuntimePath(std::string(meshUri));
  return LX_infra::scene_asset::loadSceneMeshAsset(path);
}

MeshSharedPtr makeMesh(std::vector<VertexPosNormalUvBone> verts,
                       std::vector<u32> indices,
                       const LX_core::BoundingBox &bounds,
                       const bool closedVolume = true) {
  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(std::move(indices));
  return Mesh::create(vb, ib, bounds, closedVolume);
}

void appendVertex(std::vector<VertexPosNormalUvBone> &verts, const Vec3f &pos,
                  const Vec3f &normal, const Vec2f &uv = Vec2f{0.0f, 0.0f}) {
  verts.emplace_back(pos, normal, uv, Vec4f{1.0f, 0.0f, 0.0f, 1.0f},
                     Vec4i{0, 0, 0, 0}, Vec4f{0.0f, 0.0f, 0.0f, 0.0f});
}

MeshSharedPtr buildTrianglePatchMesh() {
  std::vector<VertexPosNormalUvBone> verts;
  verts.reserve(3);
  const Vec3f up{0.0f, 1.0f, 0.0f};
  appendVertex(verts, {0.0f, 0.0f, 0.5f}, up, Vec2f{0.5f, 1.0f});
  appendVertex(verts, {-0.5f, 0.0f, -0.5f}, up, Vec2f{0.0f, 0.0f});
  appendVertex(verts, {0.5f, 0.0f, -0.5f}, up, Vec2f{1.0f, 0.0f});
  return makeMesh(
      std::move(verts), {0, 2, 1},
      LX_core::BoundingBox{{-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}}, false);
}

MeshSharedPtr buildSquarePatchMesh() {
  std::vector<VertexPosNormalUvBone> verts;
  verts.reserve(4);
  const Vec3f up{0.0f, 1.0f, 0.0f};
  appendVertex(verts, {-0.5f, 0.0f, -0.5f}, up, Vec2f{0.0f, 0.0f});
  appendVertex(verts, {0.5f, 0.0f, -0.5f}, up, Vec2f{1.0f, 0.0f});
  appendVertex(verts, {0.5f, 0.0f, 0.5f}, up, Vec2f{1.0f, 1.0f});
  appendVertex(verts, {-0.5f, 0.0f, 0.5f}, up, Vec2f{0.0f, 1.0f});
  return makeMesh(
      std::move(verts), {0, 2, 1, 0, 3, 2},
      LX_core::BoundingBox{{-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}}, false);
}

MeshSharedPtr buildCirclePatchMesh() {
  constexpr u32 segments = 48;
  constexpr float pi = 3.14159265358979323846f;
  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve(segments + 1);
  indices.reserve(segments * 3);

  const Vec3f up{0.0f, 1.0f, 0.0f};
  appendVertex(verts, {0.0f, 0.0f, 0.0f}, up, Vec2f{0.5f, 0.5f});
  for (u32 i = 0; i < segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float phi = t * 2.0f * pi;
    const float x = std::cos(phi) * 0.5f;
    const float z = std::sin(phi) * 0.5f;
    appendVertex(verts, {x, 0.0f, z}, up, Vec2f{x + 0.5f, z + 0.5f});
  }
  for (u32 i = 0; i < segments; ++i) {
    const u32 next = (i + 1u) % segments;
    indices.insert(indices.end(), {0u, next + 1u, i + 1u});
  }

  return makeMesh(
      std::move(verts), std::move(indices),
      LX_core::BoundingBox{{-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}}, false);
}

MeshSharedPtr buildPatchMesh(std::string_view meshUri) {
  if (meshUri == "builtin://lxe_editor/patches/triangle") {
    return buildTrianglePatchMesh();
  }
  if (meshUri == "builtin://lxe_editor/patches/square") {
    return buildSquarePatchMesh();
  }
  if (meshUri == "builtin://lxe_editor/patches/circle") {
    return buildCirclePatchMesh();
  }
  throw std::runtime_error("[lxe_editor] unknown builtin patch mesh URI");
}

MeshSharedPtr buildGroundMesh() {
  const float half = 20.0f; // 40m x 40m — wide enough to give visual context
  const float groundY = 0.0f;
  const Vec3f up{0.0f, 1.0f, 0.0f};
  const Vec4f tangent{1.0f, 0.0f, 0.0f, 1.0f};
  const Vec4i zeroBones{0, 0, 0, 0};
  const Vec4f zeroWeights{0.0f, 0.0f, 0.0f, 0.0f};

  std::vector<VertexPosNormalUvBone> verts;
  verts.reserve(4);
  verts.emplace_back(Vec3f{-half, groundY, -half}, up, Vec2f{0.0f, 0.0f},
                     tangent, zeroBones, zeroWeights);
  verts.emplace_back(Vec3f{half, groundY, -half}, up, Vec2f{1.0f, 0.0f},
                     tangent, zeroBones, zeroWeights);
  verts.emplace_back(Vec3f{half, groundY, half}, up, Vec2f{1.0f, 1.0f}, tangent,
                     zeroBones, zeroWeights);
  verts.emplace_back(Vec3f{-half, groundY, half}, up, Vec2f{0.0f, 1.0f},
                     tangent, zeroBones, zeroWeights);

  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(std::vector<u32>{0, 2, 1, 0, 3, 2});
  return Mesh::create(vb, ib,
                      LX_core::BoundingBox{Vec3f{-half, groundY, -half},
                                           Vec3f{half, groundY, half}});
}

LX_core::SceneNodeSharedPtr
makeRenderableNode(const char *nodeName, MeshSharedPtr mesh,
                   MaterialInstanceSharedPtr material) {
  auto node = SceneNode::create(nodeName);
  node->addComponent<LX_core::MeshComponent>(std::move(mesh));
  node->addComponent<LX_core::MaterialComponent>(std::move(material));
  return node;
}

} // namespace

LX_core::SceneNodeSharedPtr
buildHelmetNode(const std::filesystem::path &gltfPath,
                const std::filesystem::path &materialPath) {
  auto asset =
      LX_infra::scene_asset::loadGltfSceneAsset(gltfPath, materialPath);
  return makeRenderableNode("helmet", std::move(asset.mesh),
                            std::move(asset.material));
}

LX_core::MaterialInstanceSharedPtr
buildHelmetMaterial(const std::filesystem::path &gltfPath,
                    const std::filesystem::path &materialPath) {
  return LX_infra::scene_asset::loadGltfSceneAsset(gltfPath, materialPath)
      .material;
}

LX_core::SceneNodeSharedPtr buildGroundNode() {
  auto mesh = buildGroundMesh();
  auto material = makeGroundMaterial();
  return makeRenderableNode("ground", std::move(mesh), std::move(material));
}

LX_core::SceneNodeSharedPtr buildBuiltinPrimitiveNode(std::string_view meshUri,
                                                      std::string nodeName) {
  auto mesh = LX_core::buildBuiltinPrimitiveMesh(meshUri);
  auto material = makePrimitiveMaterial();
  return makeRenderableNode(nodeName.c_str(), std::move(mesh),
                            std::move(material));
}

LX_core::SceneNodeSharedPtr buildBuiltinPatchNode(std::string_view meshUri,
                                                  std::string nodeName) {
  auto mesh = buildPatchMesh(meshUri);
  auto material = makePrimitiveMaterial();
  return makeRenderableNode(nodeName.c_str(), std::move(mesh),
                            std::move(material));
}

void bindModelAlbedoTexture(LX_core::MaterialInstanceSharedPtr material,
                            std::string_view albedoTextureUri) {
  if (!material || albedoTextureUri.empty()) {
    return;
  }
  const StringID kd("Kd");
  auto existing = material->getMaterialEnvelope(kd);
  if (!existing.has_value()) {
    return;
  }

  try {
    auto sampler =
        loadCombinedTexture(resolveRuntimePath(std::string(albedoTextureUri)));
    auto envelope = existing->get();
    envelope.kind = LX_core::MaterialEnvelopeKind::Texture;
    envelope.valueType = LX_core::MaterialEnvelopeValueType::Rgb;
    envelope.uri = std::string(albedoTextureUri);
    envelope.floatValue.reset();
    envelope.rgbValue.reset();
    envelope.boolValue.reset();
    envelope.stringValue.reset();
    envelope.integerValue.reset();
    material->setMaterialEnvelope(kd, std::move(envelope));
    material->setTexture(kd, std::move(sampler));
  } catch (const std::exception &e) {
    std::cerr << "[lxe_editor] model albedo texture load failed (" << e.what()
              << "); falling back to flat color\n";
  }
  material->syncGpuData();
}

LX_core::SceneNodeSharedPtr
buildModelAssetNode(std::string_view meshUri, std::string_view materialUri,
                    std::string_view albedoTextureUri, std::string nodeName) {
  auto mesh = loadModelMesh(meshUri);
  auto material = makeModelMaterial(materialUri, albedoTextureUri);
  return makeRenderableNode(nodeName.c_str(), std::move(mesh),
                            std::move(material));
}

} // namespace LX_demo::lxe_editor
