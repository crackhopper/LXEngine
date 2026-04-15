#pragma once

#include "pipeline.hpp"
#include <string>

namespace LX_core::backend {

/// Data-driven graphics pipeline built end-to-end from a `PipelineBuildDesc`.
/// Retains a metadata shader name purely for diagnostics.
class VulkanShaderGraphicsPipeline : public VulkanPipeline {
public:
  using VulkanPipeline::VulkanPipeline;

  /// Build a fully-constructed pipeline: shader modules, descriptor set
  /// layouts, pipeline layout, and VkPipeline all in one call.
  static VulkanPipelinePtr create(VulkanDevice &device,
                                  const PipelineBuildDesc &buildInfo,
                                  VkRenderPass renderPass,
                                  std::string shaderName = {});

  std::string getPipelineId() const override { return m_shaderName; }
  std::string getShaderName() const override { return m_shaderName; }

private:
  VulkanShaderGraphicsPipeline(Token t, VulkanDevice &device,
                               const PipelineBuildDesc &buildInfo,
                               std::string shaderName);

  std::string m_shaderName;
};

} // namespace LX_core::backend
