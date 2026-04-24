#include "scene_builder.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/texture.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/mesh_loader/gltf_mesh_loader.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace LX_demo::scene_viewer {

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
using LX_core::TextureSharedPtr;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_core::Vec4i;
using LX_core::VertexBuffer;
using LX_core::VertexPosNormalUvBone;

// One-shot warning so we don't spam stderr when geometry is missing a stream.
void warnOnce(bool& flag, const char* msg) {
  if (!flag) {
    std::cerr << "[scene_viewer] " << msg << "\n";
    flag = true;
  }
}

// Build a VertexPosNormalUvBone buffer from a loaded GLTFLoader. Tangents get
// a controlled placeholder (1, 0, 0, 1) when absent — REQ-011 explicitly
// forbids MikkTSpace-style generation, and the blinnphong path we target
// stays on enableNormal=0 when tangents are unavailable so the placeholder is
// never sampled.
MeshSharedPtr buildMeshFromGltf(const infra::GLTFLoader& loader) {
  const auto& positions = loader.getPositions();
  const auto& normals = loader.getNormals();
  const auto& uvs = loader.getTexCoords();
  const auto& tangents = loader.getTangents();
  const auto& indices = loader.getIndices();

  if (positions.empty()) {
    throw std::runtime_error(
        "[scene_viewer] GLTFLoader returned empty positions");
  }
  if (indices.empty()) {
    throw std::runtime_error(
        "[scene_viewer] GLTFLoader returned empty indices");
  }

  static bool warnedNormals = false;
  static bool warnedUvs = false;
  static bool warnedTangents = false;
  if (normals.empty()) {
    warnOnce(warnedNormals, "glTF has no NORMAL stream; using {0,1,0}");
  }
  if (uvs.empty()) {
    warnOnce(warnedUvs, "glTF has no TEXCOORD_0 stream; using {0,0}");
  }
  if (tangents.empty()) {
    warnOnce(warnedTangents,
             "glTF has no TANGENT stream; using placeholder {1,0,0,1} "
             "(normal mapping stays off)");
  }

  std::vector<VertexPosNormalUvBone> verts;
  verts.reserve(positions.size());

  const Vec3f fallbackNormal{0.0f, 1.0f, 0.0f};
  const Vec2f fallbackUv{0.0f, 0.0f};
  const Vec4f fallbackTangent{1.0f, 0.0f, 0.0f, 1.0f};
  const Vec4i zeroBones{0, 0, 0, 0};
  const Vec4f zeroWeights{0.0f, 0.0f, 0.0f, 0.0f};

  for (size_t i = 0; i < positions.size(); ++i) {
    const Vec3f n = i < normals.size() ? normals[i] : fallbackNormal;
    const Vec2f uv = i < uvs.size() ? uvs[i] : fallbackUv;
    const Vec4f t = i < tangents.size() ? tangents[i] : fallbackTangent;
    verts.emplace_back(positions[i], n, uv, t, zeroBones, zeroWeights);
  }

  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(std::vector<uint32_t>(indices));
  return Mesh::create(vb, ib);
}

// Load an image file and wrap it in a CombinedTextureSampler the material
// system understands. Uses RGBA8 (stb_image always delivers 4 channels via
// STBI_rgb_alpha, which is what TextureLoader requests internally).
CombinedTextureSamplerSharedPtr loadCombinedTexture(
    const std::filesystem::path& path) {
  infra::TextureLoader loader;
  loader.load(path.string());
  const int w = loader.getWidth();
  const int h = loader.getHeight();
  if (w <= 0 || h <= 0 || loader.getData() == nullptr) {
    throw std::runtime_error("[scene_viewer] failed to load texture: "
                             + path.string());
  }

  const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
  std::vector<uint8_t> pixels(loader.getData(), loader.getData() + byteCount);

  TextureDesc desc{static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                   TextureFormat::RGBA8};
  auto tex = std::make_shared<Texture>(desc, std::move(pixels));
  return std::make_shared<CombinedTextureSampler>(std::move(tex));
}

// Bridge GLTFPbrMaterial → a Blinn-Phong material. The `.material` variant
// is chosen so the compiled shader actually exposes the bindings we touch:
// `blinnphong_textured.material` enables USE_LIGHTING + USE_UV, so
// `albedoMap` exists; `blinnphong_lit.material` is the no-texture fallback.
// Loading a variant that #ifdef's a binding out and then calling setTexture
// on it trips an internal assert in MaterialInstance.
//
// Other PBR textures (metallic/roughness, normal, occlusion, emissive) are
// read from glTF but intentionally unbound — the Blinn-Phong shader doesn't
// consume them; full PBR is a downstream REQ.
MaterialInstanceSharedPtr makeHelmetMaterial(const infra::GLTFPbrMaterial& pbr,
                                       const std::filesystem::path& gltfDir) {
  constexpr const char* kTexturedMaterial =
      "materials/blinnphong_textured.material";
  constexpr const char* kFallbackMaterial =
      "materials/blinnphong_lit.material";

  const bool hasBaseColor = !pbr.baseColorTexture.empty();
  const char* assetPath = hasBaseColor ? kTexturedMaterial : kFallbackMaterial;

  auto mat = LX_infra::loadGenericMaterial(assetPath);
  if (!mat) {
    throw std::runtime_error(std::string("[scene_viewer] failed to load ")
                             + assetPath);
  }

  // DamagedHelmet.gltf declares no TANGENT accessor — keep normal mapping off
  // so the placeholder tangent is never sampled.
  mat->setInt(StringID("enableNormal"), 0);

  if (hasBaseColor) {
    try {
      auto sampler = loadCombinedTexture(gltfDir / pbr.baseColorTexture);
      mat->setTexture(StringID("albedoMap"), std::move(sampler));
      mat->setInt(StringID("enableAlbedo"), 1);
    } catch (const std::exception& e) {
      std::cerr << "[scene_viewer] baseColor texture load failed ("
                << e.what() << "); falling back to flat color\n";
      // The textured variant is already loaded; keep enableAlbedo=0 so the
      // shader skips the (still-legal) sampler binding.
      mat->setInt(StringID("enableAlbedo"), 0);
    }
  }

  mat->syncGpuData();
  return mat;
}

MaterialInstanceSharedPtr makeGroundMaterial() {
  auto mat = LX_infra::loadGenericMaterial("materials/blinnphong_lit.material");
  if (!mat) {
    throw std::runtime_error(
        "[scene_viewer] failed to load materials/blinnphong_lit.material");
  }
  mat->setInt(StringID("enableAlbedo"), 0);
  mat->setInt(StringID("enableNormal"), 0);
  mat->setVec3(StringID("baseColor"), Vec3f{0.4f, 0.4f, 0.45f});
  mat->syncGpuData();
  return mat;
}

MeshSharedPtr buildGroundMesh() {
  const float half = 20.0f; // 40m x 40m — wide enough to give visual context
  // Ground drops below the helmet's local origin so the helmet sits clearly
  // above the plane. DamagedHelmet is roughly 2m tall centered near y=0 in
  // mesh space; we don't yet apply glTF node transforms, so hand-tune the
  // ground height here.
  const float groundY = -1.5f;
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
  verts.emplace_back(Vec3f{half, groundY, half}, up, Vec2f{1.0f, 1.0f},
                     tangent, zeroBones, zeroWeights);
  verts.emplace_back(Vec3f{-half, groundY, half}, up, Vec2f{0.0f, 1.0f},
                     tangent, zeroBones, zeroWeights);

  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(
      std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
  return Mesh::create(vb, ib);
}

} // namespace

LX_core::SceneNodeSharedPtr buildHelmetNode(const std::filesystem::path& gltfPath) {
  infra::GLTFLoader loader;
  loader.load(gltfPath.string());

  auto mesh = buildMeshFromGltf(loader);
  auto material =
      makeHelmetMaterial(loader.getMaterial(), gltfPath.parent_path());

  return SceneNode::create("helmet", std::move(mesh), std::move(material));
}

LX_core::SceneNodeSharedPtr buildGroundNode() {
  auto mesh = buildGroundMesh();
  auto material = makeGroundMaterial();
  return SceneNode::create("ground", std::move(mesh), std::move(material));
}

} // namespace LX_demo::scene_viewer
