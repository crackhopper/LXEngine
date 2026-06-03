#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

#include <span>
#include <vector>

namespace LX_core {

// Node packing matches the GPU-friendly offline BVH layout:
// - boundsMinLeftFirst.xyz stores bounds min.
// - boundsMaxCount.xyz stores bounds max.
// - Internal nodes store left child index in boundsMinLeftFirst.w and right
//   child index in boundsMaxCount.w.
// - Leaf nodes store first primitive range index in boundsMinLeftFirst.w and
//   0x80000000u | count in boundsMaxCount.w. Decode packed w fields with
//   floatBitsToUint in shaders or std::bit_cast<u32> on CPU.
//
// Leaf first/count ranges address SceneSoftwareBvh::primitives(), which is
// reordered during BVH construction. Use primitive.primitiveIndex to map a leaf
// entry back to the original SceneResourceTableUploadView::primitives order.
struct alignas(16) SceneSoftwareBvhNode final {
  Vec4f boundsMinLeftFirst{};
  Vec4f boundsMaxCount{};
};

struct SceneSoftwareBvhPrimitive final {
  u32 primitiveIndex = 0;
  u32 objectIndex = 0;
  u32 meshIndex = 0;
};

class SceneSoftwareBvh final {
public:
  [[nodiscard]] static SceneSoftwareBvh
  build(const SceneResourceTableUploadView &scene);

  [[nodiscard]] std::span<const SceneSoftwareBvhNode> nodes() const;
  [[nodiscard]] std::span<const SceneSoftwareBvhPrimitive> primitives() const;
  [[nodiscard]] usize primitiveCount() const;

private:
  std::vector<SceneSoftwareBvhNode> m_nodes;
  std::vector<SceneSoftwareBvhPrimitive> m_primitives;
};

} // namespace LX_core
