#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/render_effect.hpp"
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
当前 pipeline layout 仍能描述 shader 反射得到的 push constant 范围；默认
render work 不再用它承载每 draw 数据。

这里保存的是 pipeline layout 需要的结构信息，不是每个 draw 的实际数据值。
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
@source_analysis.section PipelineBuildDesc：从 legacy helper work 派生出的构建输入包
`PipelineKey` 只回答“是不是同一条 pipeline”；`PipelineBuildDesc` 回答
“如果这条 pipeline 还没建，backend 需要哪些输入”。

它从一个已经校验好的 `RenderWorkItem` 派生，不重新判断材质是否合法，也不重新推导
identity。073-e 之后，realtime material-source geometry 的正向 pipeline lookup
应从 `RenderBatch` 与 node context 派生；这个入口只保留给 compute/offline 与
fullscreen/IBL/debug 这类显式 direct raster helper。
*/
struct PipelineBuildDesc {
  PipelineBuildType type = PipelineBuildType::Graphics;
  PipelineKey key;
  StringID shaderVariantKey;
  RenderTargetDesc target;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::vector<RenderPathAttachmentContract> attachments;
  std::vector<ShaderStageCode> stages;
  std::vector<ShaderResourceBinding> bindings;
  VertexLayout vertexLayout;
  RenderState renderState;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  PushConstantRange pushConstant;

  /// Derive a complete PipelineBuildDesc from a fully-built helper work item.
  /// Direct raster helpers require shader, vertex, and index resources.
  /// Compute items require shader stages and descriptor bindings only.
  static PipelineBuildDesc fromRenderWorkItem(const RenderWorkItem &item);
};

} // namespace LX_core
