#include "core/raytracing/software_bvh.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace LX_core {
namespace {

constexpr u32 LeafNodeFlag = 0x80000000u;

struct Bounds final {
  Vec3f min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  Vec3f max{-std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};

  void include(const Vec3f &point) {
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
  }

  void include(const Bounds &bounds) {
    include(bounds.min);
    include(bounds.max);
  }

  [[nodiscard]] Vec3f extent() const { return max - min; }
};

[[nodiscard]] float uintAsFloat(u32 value) { return std::bit_cast<float>(value); }

[[nodiscard]] Vec3f transformPoint(const std::array<Vec4f, 4> &matrix,
                                   const Vec3f &point) {
  const float x = matrix[0].x * point.x + matrix[1].x * point.y +
                  matrix[2].x * point.z + matrix[3].x;
  const float y = matrix[0].y * point.x + matrix[1].y * point.y +
                  matrix[2].y * point.z + matrix[3].y;
  const float z = matrix[0].z * point.x + matrix[1].z * point.y +
                  matrix[2].z * point.z + matrix[3].z;
  const float w = matrix[0].w * point.x + matrix[1].w * point.y +
                  matrix[2].w * point.z + matrix[3].w;
  if (w != 0.0f && w != 1.0f) {
    return Vec3f{x / w, y / w, z / w};
  }
  return Vec3f{x, y, z};
}

[[nodiscard]] const SceneGpuPrimitiveRecord &
gpuPrimitive(const SceneResourceTableUploadView &scene,
             const SceneSoftwareBvhPrimitive &primitive) {
  return scene.primitives[primitive.primitiveIndex];
}

[[nodiscard]] Bounds primitiveBounds(
    const SceneResourceTableUploadView &scene,
    const SceneSoftwareBvhPrimitive &primitive) {
  const SceneGpuPrimitiveRecord &gpuRecord = gpuPrimitive(scene, primitive);
  const SceneGpuObjectRecord &object = scene.objects[gpuRecord.objectIndex];
  Bounds bounds;
  for (u32 i = 0; i < 3; ++i) {
    const u32 index = scene.indices[gpuRecord.indexOffset + i];
    const SceneGpuVertexRecord &vertex = scene.vertices[index];
    bounds.include(transformPoint(object.objectToWorld,
                                  vertex.position.toVec3()));
  }
  return bounds;
}

[[nodiscard]] Vec3f primitiveCentroid(
    const SceneResourceTableUploadView &scene,
    const SceneSoftwareBvhPrimitive &primitive) {
  const Bounds bounds = primitiveBounds(scene, primitive);
  return (bounds.min + bounds.max) * 0.5f;
}

u32 buildNode(std::vector<SceneSoftwareBvhNode> &nodes,
              std::vector<SceneSoftwareBvhPrimitive> &primitives,
              const SceneResourceTableUploadView &scene, const u32 first,
              const u32 count) {
  const u32 nodeIndex = static_cast<u32>(nodes.size());
  nodes.push_back({});

  Bounds bounds;
  Bounds centroidBounds;
  for (u32 i = first; i < first + count; ++i) {
    bounds.include(primitiveBounds(scene, primitives[i]));
    centroidBounds.include(primitiveCentroid(scene, primitives[i]));
  }

  if (count <= 4) {
    nodes[nodeIndex].boundsMinLeftFirst =
        Vec4f{bounds.min.x, bounds.min.y, bounds.min.z, uintAsFloat(first)};
    nodes[nodeIndex].boundsMaxCount =
        Vec4f{bounds.max.x, bounds.max.y, bounds.max.z,
              uintAsFloat(LeafNodeFlag | count)};
    return nodeIndex;
  }

  const Vec3f extent = centroidBounds.extent();
  int axis = 0;
  if (extent.y > extent.x && extent.y >= extent.z) {
    axis = 1;
  } else if (extent.z > extent.x && extent.z > extent.y) {
    axis = 2;
  }

  const u32 mid = first + count / 2;
  std::nth_element(
      primitives.begin() + first, primitives.begin() + mid,
      primitives.begin() + first + count,
      [&scene, axis](const SceneSoftwareBvhPrimitive &left,
                     const SceneSoftwareBvhPrimitive &right) {
        return primitiveCentroid(scene, left)[axis] <
               primitiveCentroid(scene, right)[axis];
      });

  const u32 left = buildNode(nodes, primitives, scene, first, mid - first);
  const u32 right = buildNode(nodes, primitives, scene, mid, first + count - mid);
  nodes[nodeIndex].boundsMinLeftFirst =
      Vec4f{bounds.min.x, bounds.min.y, bounds.min.z, uintAsFloat(left)};
  nodes[nodeIndex].boundsMaxCount =
      Vec4f{bounds.max.x, bounds.max.y, bounds.max.z, uintAsFloat(right)};
  return nodeIndex;
}

} // namespace

SceneSoftwareBvh SceneSoftwareBvh::build(
    const SceneResourceTableUploadView &scene) {
  if (scene.primitives.empty()) {
    throw std::runtime_error("cannot build software BVH for empty primitive list");
  }

  SceneSoftwareBvh bvh;
  bvh.m_primitives.reserve(scene.primitives.size());
  for (u32 i = 0; i < scene.primitives.size(); ++i) {
    const SceneGpuPrimitiveRecord &primitive = scene.primitives[i];
    bvh.m_primitives.push_back(SceneSoftwareBvhPrimitive{
        .primitiveIndex = i,
        .objectIndex = primitive.objectIndex,
        .meshIndex = primitive.meshIndex,
    });
  }
  buildNode(bvh.m_nodes, bvh.m_primitives, scene, 0,
            static_cast<u32>(bvh.m_primitives.size()));
  return bvh;
}

std::span<const SceneSoftwareBvhNode> SceneSoftwareBvh::nodes() const {
  return m_nodes;
}

std::span<const SceneSoftwareBvhPrimitive>
SceneSoftwareBvh::primitives() const {
  return m_primitives;
}

usize SceneSoftwareBvh::primitiveCount() const { return m_primitives.size(); }

} // namespace LX_core
