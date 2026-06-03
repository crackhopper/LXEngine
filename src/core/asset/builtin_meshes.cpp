#include "core/asset/builtin_meshes.hpp"

#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace LX_core {
namespace {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] MeshSharedPtr
makeMesh(std::vector<VertexPosNormalUvBone> verts, std::vector<u32> indices,
         const BoundingBox &bounds, const bool closedVolume = true) {
  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(verts));
  auto ib = IndexBuffer::create(std::move(indices));
  return Mesh::create(vb, ib, bounds, closedVolume);
}

void appendVertex(std::vector<VertexPosNormalUvBone> &verts, const Vec3f &pos,
                  const Vec3f &normal,
                  const Vec2f &uv = Vec2f{0.0f, 0.0f}) {
  verts.emplace_back(pos, normal, uv, Vec4f{1.0f, 0.0f, 0.0f, 1.0f},
                     Vec4i{0, 0, 0, 0}, Vec4f{0.0f, 0.0f, 0.0f, 0.0f});
}

[[nodiscard]] MeshSharedPtr buildCubeMesh() {
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
      BoundingBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
}

[[nodiscard]] MeshSharedPtr buildPlaneMesh() {
  constexpr float half = 0.5f;
  constexpr float top = 0.0f;
  constexpr float bottom = -0.02f;

  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve(24);
  indices.reserve(36);

  const Vec3f normals[] = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
                           {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                           {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
  const Vec3f faces[][4] = {
      {{-half, bottom, half},
       {half, bottom, half},
       {half, top, half},
       {-half, top, half}},
      {{half, bottom, -half},
       {-half, bottom, -half},
       {-half, top, -half},
       {half, top, -half}},
      {{half, bottom, half},
       {half, bottom, -half},
       {half, top, -half},
       {half, top, half}},
      {{-half, bottom, -half},
       {-half, bottom, half},
       {-half, top, half},
       {-half, top, -half}},
      {{-half, top, half},
       {half, top, half},
       {half, top, -half},
       {-half, top, -half}},
      {{-half, bottom, -half},
       {half, bottom, -half},
       {half, bottom, half},
       {-half, bottom, half}},
  };
  for (u32 f = 0; f < 6; ++f) {
    const u32 base = static_cast<u32>(verts.size());
    for (u32 i = 0; i < 4; ++i) {
      appendVertex(verts, faces[f][i], normals[f]);
    }
    indices.insert(indices.end(),
                   {base, base + 1, base + 2, base, base + 2, base + 3});
  }

  return makeMesh(std::move(verts), std::move(indices),
                  BoundingBox{{-half, bottom, -half}, {half, top, half}});
}

[[nodiscard]] MeshSharedPtr buildSphereMesh() {
  constexpr u32 rings = 12;
  constexpr u32 segments = 24;
  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve((rings + 1) * (segments + 1));
  for (u32 r = 0; r <= rings; ++r) {
    const float v = static_cast<float>(r) / static_cast<float>(rings);
    const float theta = v * kPi;
    for (u32 s = 0; s <= segments; ++s) {
      const float u = static_cast<float>(s) / static_cast<float>(segments);
      const float phi = u * 2.0f * kPi;
      const Vec3f normal{std::sin(theta) * std::cos(phi), std::cos(theta),
                         std::sin(theta) * std::sin(phi)};
      appendVertex(verts, normal * 0.5f, normal, Vec2f{u, v});
    }
  }
  for (u32 r = 0; r < rings; ++r) {
    for (u32 s = 0; s < segments; ++s) {
      const u32 a = r * (segments + 1) + s;
      const u32 b = a + segments + 1;
      indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
  }
  return makeMesh(
      std::move(verts), std::move(indices),
      BoundingBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
}

[[nodiscard]] MeshSharedPtr buildCylinderMesh(const bool cone) {
  constexpr u32 segments = 32;
  std::vector<VertexPosNormalUvBone> verts;
  std::vector<u32> indices;
  verts.reserve(segments * 4 + 2);
  for (u32 s = 0; s < segments; ++s) {
    const float u = static_cast<float>(s) / static_cast<float>(segments);
    const float phi = u * 2.0f * kPi;
    const Vec3f radial{std::cos(phi), 0.0f, std::sin(phi)};
    appendVertex(verts, radial * 0.5f + Vec3f{0.0f, -0.5f, 0.0f}, radial);
    appendVertex(verts,
                 cone ? Vec3f{0.0f, 0.5f, 0.0f}
                      : radial * 0.5f + Vec3f{0.0f, 0.5f, 0.0f},
                 cone ? Vec3f{radial.x, 0.5f, radial.z}.normalized()
                      : radial);
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
      BoundingBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
}

} // namespace

bool isBuiltinPrimitiveMeshUri(std::string_view meshUri) {
  return meshUri == "builtin://lxe_editor/primitives/cube" ||
         meshUri == "builtin://lxe_editor/primitives/sphere" ||
         meshUri == "builtin://lxe_editor/primitives/plane" ||
         meshUri == "builtin://lxe_editor/primitives/cylinder" ||
         meshUri == "builtin://lxe_editor/primitives/cone";
}

MeshSharedPtr buildBuiltinPrimitiveMesh(std::string_view meshUri) {
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
  throw std::runtime_error("unknown builtin primitive mesh URI: " +
                           std::string(meshUri));
}

} // namespace LX_core
