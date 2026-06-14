#include "core/pipeline/pipeline_build_desc.hpp"

#include <utility>

namespace LX_core {

PipelineBuildDesc PipelineBuildDesc::graphics(
    PipelineKey key, StringID shaderVariantKey, RenderTargetDesc target,
    std::vector<ShaderStageCode> stages,
    std::vector<ShaderResourceBinding> bindings, VertexLayout vertexLayout,
    RenderState renderState, PrimitiveTopology topology,
    std::optional<RenderPathNodeRenderingMode> renderingMode,
    std::vector<RenderPathAttachmentContract> attachments) {
  PipelineBuildDesc desc;
  desc.type = PipelineBuildType::Graphics;
  desc.key = key;
  desc.shaderVariantKey = shaderVariantKey;
  desc.target = target;
  desc.stages = std::move(stages);
  desc.bindings = std::move(bindings);
  desc.vertexLayout = std::move(vertexLayout);
  desc.renderState = renderState;
  desc.topology = topology;
  desc.renderingMode = renderingMode;
  desc.attachments = std::move(attachments);
  desc.pushConstant = PushConstantRange{};
  return desc;
}

PipelineBuildDesc PipelineBuildDesc::compute(
    PipelineKey key, StringID shaderVariantKey,
    std::vector<ShaderStageCode> stages,
    std::vector<ShaderResourceBinding> bindings) {
  PipelineBuildDesc desc;
  desc.type = PipelineBuildType::Compute;
  desc.key = key;
  desc.shaderVariantKey = shaderVariantKey;
  desc.stages = std::move(stages);
  desc.bindings = std::move(bindings);
  desc.pushConstant = PushConstantRange{};
  desc.pushConstant.size = 0;
  desc.pushConstant.stageFlagsMask =
      static_cast<ShaderStageMask32>(ShaderStage::Compute);
  return desc;
}

} // namespace LX_core
