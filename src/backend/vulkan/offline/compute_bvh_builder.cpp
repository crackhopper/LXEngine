#include "backend/vulkan/offline/compute_bvh_builder.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace LX_core::backend::offline {
namespace {

struct Bounds final {
  Vec3f min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  Vec3f max{-std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};

  void include(const Vec3f &p) {
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
  }

  void include(const Bounds &b) {
    include(b.min);
    include(b.max);
  }

  [[nodiscard]] Vec3f extent() const { return max - min; }
};

[[nodiscard]] Vec3f xyz(const Vec4f &v) { return Vec3f{v.x, v.y, v.z}; }

[[nodiscard]] Bounds triangleBounds(const GpuTriangle &tri) {
  Bounds b;
  b.include(xyz(tri.v0));
  b.include(xyz(tri.v1));
  b.include(xyz(tri.v2));
  return b;
}

[[nodiscard]] Vec3f triangleCentroid(const GpuTriangle &tri) {
  return (xyz(tri.v0) + xyz(tri.v1) + xyz(tri.v2)) / 3.0f;
}

[[nodiscard]] float uintAsFloat(u32 value) {
  return std::bit_cast<float>(value);
}

constexpr u32 LeafNodeFlag = 0x80000000u;

u32 buildNode(std::vector<GpuTriangle> &triangles, std::vector<GpuBvhNode> &nodes,
              u32 first, u32 count) {
  const u32 nodeIndex = static_cast<u32>(nodes.size());
  nodes.push_back({});

  Bounds bounds;
  Bounds centroidBounds;
  for (u32 i = first; i < first + count; ++i) {
    bounds.include(triangleBounds(triangles[i]));
    centroidBounds.include(triangleCentroid(triangles[i]));
  }

  if (count <= 4) {
    nodes[nodeIndex].boundsMinLeftFirst =
        Vec4f{bounds.min.x, bounds.min.y, bounds.min.z, uintAsFloat(first)};
    nodes[nodeIndex].boundsMaxTriCount =
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
  std::nth_element(triangles.begin() + first, triangles.begin() + mid,
                   triangles.begin() + first + count,
                   [axis](const GpuTriangle &a, const GpuTriangle &b) {
                     return triangleCentroid(a)[axis] < triangleCentroid(b)[axis];
                   });

  const u32 left = buildNode(triangles, nodes, first, mid - first);
  const u32 right = buildNode(triangles, nodes, mid, first + count - mid);
  nodes[nodeIndex].boundsMinLeftFirst =
      Vec4f{bounds.min.x, bounds.min.y, bounds.min.z, uintAsFloat(left)};
  nodes[nodeIndex].boundsMaxTriCount =
      Vec4f{bounds.max.x, bounds.max.y, bounds.max.z, uintAsFloat(right)};
  return nodeIndex;
}

} // namespace

BvhBuildResult ComputeBvhBuilder::build(std::vector<GpuTriangle> triangles) const {
  if (triangles.empty()) {
    throw std::runtime_error("cannot build offline BVH for empty triangle list");
  }
  BvhBuildResult result;
  result.triangles = std::move(triangles);
  buildNode(result.triangles, result.nodes, 0,
            static_cast<u32>(result.triangles.size()));
  return result;
}

} // namespace LX_core::backend::offline
