#include "scene_builder.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/texture.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/mesh_loader/gltf_mesh_loader.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
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
using LX_core::TextureSharedPtr;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_core::Vec4i;
using LX_core::VertexBuffer;
using LX_core::VertexPosNormalUvBone;

// One-shot warning so we don't spam stderr when geometry is missing a stream.
void warnOnce(bool &flag, const char *msg) {
  if (!flag) {
    std::cerr << "[lxe_editor] " << msg << "\n";
    flag = true;
  }
}

// Build a VertexPosNormalUvBone buffer from a loaded GLTFLoader. Tangents get
// a controlled placeholder (1, 0, 0, 1) when absent — REQ-011 explicitly
// forbids MikkTSpace-style generation, and the blinnphong path we target
// stays on enableNormal=0 when tangents are unavailable so the placeholder is
// never sampled.
MeshSharedPtr buildMeshFromGltf(const infra::GLTFLoader &loader) {
  const auto &positions = loader.getPositions();
  const auto &normals = loader.getNormals();
  const auto &uvs = loader.getTexCoords();
  const auto &tangents = loader.getTangents();
  const auto &indices = loader.getIndices();

  if (positions.empty()) {
    throw std::runtime_error(
        "[lxe_editor] GLTFLoader returned empty positions");
  }
  if (indices.empty()) {
    throw std::runtime_error("[lxe_editor] GLTFLoader returned empty indices");
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

  for (usize i = 0; i < positions.size(); ++i) {
    const Vec3f n = i < normals.size() ? normals[i] : fallbackNormal;
    const Vec2f uv = i < uvs.size() ? uvs[i] : fallbackUv;
    const Vec4f t = i < tangents.size() ? tangents[i] : fallbackTangent;
    verts.emplace_back(positions[i], n, uv, t, zeroBones, zeroWeights);
  }

  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(std::vector<u32>(indices));
  return Mesh::create(vb, ib, loader.getBounds());
}

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
MaterialInstanceSharedPtr
makeHelmetMaterial(const infra::GLTFPbrMaterial &pbr,
                   const std::filesystem::path &gltfDir) {
  constexpr const char *kTexturedMaterial =
      "assets/materials/blinnphong_textured.material";
  constexpr const char *kFallbackMaterial =
      "assets/materials/blinnphong_lit.material";

  const bool hasBaseColor = !pbr.baseColorTexture.empty();
  const char *assetPath = hasBaseColor ? kTexturedMaterial : kFallbackMaterial;

  auto mat = LX_infra::loadGenericMaterial(assetPath);
  if (!mat) {
    throw std::runtime_error(std::string("[lxe_editor] failed to load ") +
                             assetPath);
  }

  // DamagedHelmet.gltf declares no TANGENT accessor — keep normal mapping off
  // so the placeholder tangent is never sampled.
  mat->setParameter(StringID("MaterialUBO"), StringID("enableNormal"), 0);

  if (hasBaseColor) {
    try {
      auto sampler = loadCombinedTexture(gltfDir / pbr.baseColorTexture);
      mat->setTexture(StringID("albedoMap"), std::move(sampler));
      mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 1);
    } catch (const std::exception &e) {
      std::cerr << "[lxe_editor] baseColor texture load failed (" << e.what()
                << "); falling back to flat color\n";
      // The textured variant is already loaded; keep enableAlbedo=0 so the
      // shader skips the (still-legal) sampler binding.
      mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 0);
    }
  }

  mat->syncGpuData();
  return mat;
}

MaterialInstanceSharedPtr makeGroundMaterial() {
  auto mat =
      LX_infra::loadGenericMaterial("assets/materials/blinnphong_lit.material");
  if (!mat) {
    throw std::runtime_error(
        "[lxe_editor] failed to load assets/materials/blinnphong_lit.material");
  }
  mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 0);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableNormal"), 0);
  mat->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                    Vec3f{0.4f, 0.4f, 0.45f});
  mat->syncGpuData();
  return mat;
}

MaterialInstanceSharedPtr makePrimitiveMaterial() {
  auto mat =
      LX_infra::loadGenericMaterial("assets/materials/blinnphong_lit.material");
  if (!mat) {
    throw std::runtime_error(
        "[lxe_editor] failed to load assets/materials/blinnphong_lit.material");
  }
  mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 0);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableNormal"), 0);
  mat->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                    Vec3f{0.72f, 0.74f, 0.78f});
  mat->syncGpuData();
  return mat;
}

MeshSharedPtr makeMesh(std::vector<VertexPosNormalUvBone> verts,
                       std::vector<u32> indices,
                       const LX_core::BoundingBox &bounds) {
  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(std::move(indices));
  return Mesh::create(vb, ib, bounds);
}

void appendVertex(std::vector<VertexPosNormalUvBone> &verts, const Vec3f &pos,
                  const Vec3f &normal, const Vec2f &uv = Vec2f{0.0f, 0.0f}) {
  verts.emplace_back(pos, normal, uv, Vec4f{1.0f, 0.0f, 0.0f, 1.0f},
                     Vec4i{0, 0, 0, 0}, Vec4f{0.0f, 0.0f, 0.0f, 0.0f});
}

MeshSharedPtr buildCubeMesh() {
  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve(24);
  indices.reserve(36);
  const Vec3f normals[] = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
                           {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                           {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
  const Vec3f faces[][4] = {
      {{-0.5f, -0.5f, 0.5f},
       {0.5f, -0.5f, 0.5f},
       {0.5f, 0.5f, 0.5f},
       {-0.5f, 0.5f, 0.5f}},
      {{0.5f, -0.5f, -0.5f},
       {-0.5f, -0.5f, -0.5f},
       {-0.5f, 0.5f, -0.5f},
       {0.5f, 0.5f, -0.5f}},
      {{0.5f, -0.5f, 0.5f},
       {0.5f, -0.5f, -0.5f},
       {0.5f, 0.5f, -0.5f},
       {0.5f, 0.5f, 0.5f}},
      {{-0.5f, -0.5f, -0.5f},
       {-0.5f, -0.5f, 0.5f},
       {-0.5f, 0.5f, 0.5f},
       {-0.5f, 0.5f, -0.5f}},
      {{-0.5f, 0.5f, 0.5f},
       {0.5f, 0.5f, 0.5f},
       {0.5f, 0.5f, -0.5f},
       {-0.5f, 0.5f, -0.5f}},
      {{-0.5f, -0.5f, -0.5f},
       {0.5f, -0.5f, -0.5f},
       {0.5f, -0.5f, 0.5f},
       {-0.5f, -0.5f, 0.5f}},
  };
  for (u32 f = 0; f < 6; ++f) {
    const u32 base = static_cast<u32>(verts.size());
    for (u32 i = 0; i < 4; ++i) {
      appendVertex(verts, faces[f][i], normals[f]);
    }
    indices.insert(indices.end(),
                   {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  return makeMesh(
      std::move(verts), std::move(indices),
      LX_core::BoundingBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
}

MeshSharedPtr buildPlaneMesh() {
  std::vector<VertexPosNormalUvBone> verts;
  verts.reserve(4);
  const Vec3f up{0.0f, 1.0f, 0.0f};
  appendVertex(verts, {-0.5f, 0.0f, -0.5f}, up);
  appendVertex(verts, {0.5f, 0.0f, -0.5f}, up);
  appendVertex(verts, {0.5f, 0.0f, 0.5f}, up);
  appendVertex(verts, {-0.5f, 0.0f, 0.5f}, up);
  return makeMesh(
      std::move(verts), {0, 2, 1, 0, 3, 2},
      LX_core::BoundingBox{{-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}});
}

MeshSharedPtr buildSphereMesh() {
  constexpr u32 rings = 12;
  constexpr u32 segments = 24;
  constexpr float pi = 3.14159265358979323846f;
  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve((rings + 1) * (segments + 1));
  for (u32 r = 0; r <= rings; ++r) {
    const float v = static_cast<float>(r) / static_cast<float>(rings);
    const float theta = v * pi;
    for (u32 s = 0; s <= segments; ++s) {
      const float u = static_cast<float>(s) / static_cast<float>(segments);
      const float phi = u * 2.0f * pi;
      const Vec3f normal{std::sin(theta) * std::cos(phi), std::cos(theta),
                         std::sin(theta) * std::sin(phi)};
      appendVertex(verts, normal * 0.5f, normal, Vec2f{u, v});
    }
  }
  for (u32 r = 0; r < rings; ++r) {
    for (u32 s = 0; s < segments; ++s) {
      const u32 a = r * (segments + 1) + s;
      const u32 b = a + segments + 1;
      indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
  }
  return makeMesh(
      std::move(verts), std::move(indices),
      LX_core::BoundingBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
}

MeshSharedPtr buildCylinderMesh(const bool cone) {
  constexpr u32 segments = 32;
  constexpr float pi = 3.14159265358979323846f;
  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve(segments * 4 + 2);
  for (u32 s = 0; s < segments; ++s) {
    const float u = static_cast<float>(s) / static_cast<float>(segments);
    const float phi = u * 2.0f * pi;
    const Vec3f radial{std::cos(phi), 0.0f, std::sin(phi)};
    appendVertex(verts, radial * 0.5f + Vec3f{0.0f, -0.5f, 0.0f}, radial);
    appendVertex(verts,
                 cone ? Vec3f{0.0f, 0.5f, 0.0f}
                      : radial * 0.5f + Vec3f{0.0f, 0.5f, 0.0f},
                 cone ? Vec3f{radial.x, 0.5f, radial.z}.normalized() : radial);
  }
  for (u32 s = 0; s < segments; ++s) {
    const u32 next = (s + 1) % segments;
    const u32 a = s * 2;
    const u32 b = next * 2;
    if (cone) {
      indices.insert(indices.end(), {a, b, a + 1});
    } else {
      indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
  }
  const u32 bottomCenter = static_cast<u32>(verts.size());
  appendVertex(verts, {0.0f, -0.5f, 0.0f}, {0.0f, -1.0f, 0.0f});
  const u32 topCenter = static_cast<u32>(verts.size());
  appendVertex(verts, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f});
  for (u32 s = 0; s < segments; ++s) {
    const u32 next = (s + 1) % segments;
    indices.insert(indices.end(), {bottomCenter, next * 2, s * 2});
    if (!cone) {
      indices.insert(indices.end(), {topCenter, s * 2 + 1, next * 2 + 1});
    }
  }
  return makeMesh(
      std::move(verts), std::move(indices),
      LX_core::BoundingBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
}

MeshSharedPtr buildPrimitiveMesh(std::string_view meshUri) {
  if (meshUri == "builtin://lxe_editor/primitives/cube") {
    return buildCubeMesh();
  }
  if (meshUri == "builtin://lxe_editor/primitives/sphere") {
    return buildSphereMesh();
  }
  if (meshUri == "builtin://lxe_editor/primitives/plane") {
    return buildPlaneMesh();
  }
  if (meshUri == "builtin://lxe_editor/primitives/cylinder") {
    return buildCylinderMesh(false);
  }
  if (meshUri == "builtin://lxe_editor/primitives/cone") {
    return buildCylinderMesh(true);
  }
  throw std::runtime_error("[lxe_editor] unknown builtin primitive mesh URI");
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
buildHelmetNode(const std::filesystem::path &gltfPath) {
  infra::GLTFLoader loader;
  loader.load(gltfPath.string());

  auto mesh = buildMeshFromGltf(loader);
  auto material =
      makeHelmetMaterial(loader.getMaterial(), gltfPath.parent_path());

  return makeRenderableNode("helmet", std::move(mesh), std::move(material));
}

LX_core::SceneNodeSharedPtr buildGroundNode() {
  auto mesh = buildGroundMesh();
  auto material = makeGroundMaterial();
  return makeRenderableNode("ground", std::move(mesh), std::move(material));
}

LX_core::SceneNodeSharedPtr buildBuiltinPrimitiveNode(std::string_view meshUri,
                                                      std::string nodeName) {
  auto mesh = buildPrimitiveMesh(meshUri);
  auto material = makePrimitiveMaterial();
  return makeRenderableNode(nodeName.c_str(), std::move(mesh),
                            std::move(material));
}

} // namespace LX_demo::lxe_editor
