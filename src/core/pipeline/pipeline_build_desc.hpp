#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include <cstdint>
#include <vector>

namespace LX_core {

struct RenderWorkItem; // forward decl

enum class PipelineBuildType {
  Graphics,
  Compute,
  RayTracing,
};

/*
@source_analysis.section PushConstantRange：当前固定 ABI 的占位描述
当前 forward draw path 的 push constant ABI 已收敛到 model-only 数据，但
backend-neutral 层仍用 `PushConstantRange` 描述“pipeline 创建时需要声明的范围”。

这里保存的是 pipeline layout 需要的结构信息，不是每个 draw 的实际 push constant
值。真实的 per-draw 数据在 `RenderWorkItem::raster.drawData` /
`PerDrawData` 路径上传。
*/
struct PushConstantRange {
  u32 offset = 0;
  u32 size = 128;
  /// Bitmask of ShaderStage values that need access. Default: Vertex |
  /// Fragment.
  ShaderStageMask32 stageFlagsMask =
      static_cast<ShaderStageMask32>(ShaderStage::Vertex) |
      static_cast<ShaderStageMask32>(ShaderStage::Fragment);
};

/*
@source_analysis.section PipelineBuildDesc：从 RenderWorkItem 派生出的构建输入包
`PipelineKey` 只回答“是不是同一条 pipeline”；`PipelineBuildDesc` 回答
“如果这条 pipeline 还没建，backend 需要哪些输入”。

它从一个已经校验好的 `RenderWorkItem` 派生，不重新判断材质是否合法，也不重新推导
identity。这样前端的 SceneNode/RenderWorkQueue 负责把 draw 事实准备好，backend
只负责把 这些事实翻译成 Vulkan pipeline 创建参数。
*/
struct PipelineBuildDesc {
  PipelineBuildType type = PipelineBuildType::Graphics;
  PipelineKey key;
  RenderTargetDesc target;
  std::vector<ShaderStageCode> stages;
  std::vector<ShaderResourceBinding> bindings;
  VertexLayout vertexLayout;
  RenderState renderState;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  PushConstantRange pushConstant;

  /// Derive a complete PipelineBuildDesc from a fully-built RenderWorkItem.
  /// Graphics items require shader, material, vertex, and index resources.
  /// Compute items require shader stages and descriptor bindings only.
  static PipelineBuildDesc fromRenderWorkItem(const RenderWorkItem &item);
};

} // namespace LX_core
