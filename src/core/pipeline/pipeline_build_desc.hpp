#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/render_effect.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace LX_core {

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
@source_analysis.section PipelineBuildDesc：backend 构建 pipeline 的事实包
`PipelineKey` 只回答“是不是同一条 pipeline”；`PipelineBuildDesc` 回答
“如果这条 pipeline 还没建，backend 需要哪些输入”。

它不再从旧 work item / batch 反向推导。调用方必须在 RenderWorkCompiler prepare
阶段或显式 backend 过渡调用点先准备好 shader、layout、render state、target 和
attachment 等结构事实，然后用 `graphics` / `compute` 直接构造。
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
  std::vector<ShaderSpecializationConstant> specializationConstants;
  VertexLayout vertexLayout;
  RenderState renderState;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  PushConstantRange pushConstant;

  static PipelineBuildDesc
  graphics(PipelineKey key, StringID shaderVariantKey, RenderTargetDesc target,
           std::vector<ShaderStageCode> stages,
           std::vector<ShaderResourceBinding> bindings,
           VertexLayout vertexLayout, RenderState renderState,
           PrimitiveTopology topology,
           std::optional<RenderPathNodeRenderingMode> renderingMode,
           std::vector<RenderPathAttachmentContract> attachments,
           std::vector<ShaderSpecializationConstant> specializationConstants =
               {});

  static PipelineBuildDesc
  compute(PipelineKey key, StringID shaderVariantKey,
          std::vector<ShaderStageCode> stages,
          std::vector<ShaderResourceBinding> bindings,
          std::vector<ShaderSpecializationConstant> specializationConstants =
              {});
};

} // namespace LX_core
