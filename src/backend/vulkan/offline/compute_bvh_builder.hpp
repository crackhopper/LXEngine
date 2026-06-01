#pragma once

#include "backend/vulkan/offline/gpu_scene_builder.hpp"
#include "core/math/vec.hpp"

#include <vector>

namespace LX_core::backend::offline {

/*
@source_analysis.section Compute BVH 是当前 shader 的遍历索引
`ComputeBvhBuilder` 在 CPU 上为 triangle buffer 构建一棵紧凑 BVH，然后把节点
上传给 compute shader。当前节点布局把 bounds、left/first 和 packed
right/triCount 放进两个 `vec4`，保持 32 字节 std430 合同。

这不是最终高性能加速结构，而是 MVP 的可验证起点。它让离线 renderer 先拥有
closest-hit 查询、shadow ray 查询和后续 path tracing 的基础空间索引；未来可以
替换 split 策略、实例层 BVH 或 Vulkan hardware ray tracing，但 shader 与测试必须
同步迁移节点编码。
*/
struct alignas(16) GpuBvhNode final {
  Vec4f boundsMinLeftFirst{};
  Vec4f boundsMaxTriCount{};
};

struct BvhBuildResult final {
  std::vector<GpuTriangle> triangles;
  std::vector<GpuBvhNode> nodes;
};

class ComputeBvhBuilder final {
public:
  [[nodiscard]] BvhBuildResult build(std::vector<GpuTriangle> triangles) const;
};

static_assert(sizeof(GpuBvhNode) == 32);

} // namespace LX_core::backend::offline
