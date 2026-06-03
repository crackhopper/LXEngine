#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

#include <span>
#include <vector>

namespace LX_core {

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
