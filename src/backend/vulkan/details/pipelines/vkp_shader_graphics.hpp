#pragma once

#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "vkp_pipeline.hpp"
#include <string>
#include <vector>

namespace LX_core::backend {

/// File-based graphics pipeline with explicit vertex layout, topology, and slot
/// table (replaces shader-specific subclasses such as Blinn-Phong).
class VulkanShaderGraphicsPipeline : public VulkanPipeline {
public:
  using VulkanPipeline::VulkanPipeline;

  static VulkanPipelinePtr
  create(VulkanDevice &device, VkExtent2D extent, std::string shaderBaseName,
         VertexLayout vertexLayout, std::vector<PipelineSlotDetails> slots,
         PushConstantDetails pushConstants, PrimitiveTopology topology);

  const VertexLayout &referenceVertexLayout() const override;
  std::string getShaderName() const override;
  std::string getPipelineId() const override;

  VkPipelineInputAssemblyStateCreateInfo getInputAssemblyStateCreateInfo() override;

private:
  VulkanShaderGraphicsPipeline(Token t, VulkanDevice &device, VkExtent2D extent,
                               std::string shaderBaseName, VertexLayout vertexLayout,
                               std::vector<PipelineSlotDetails> slots,
                               PushConstantDetails pushConstants,
                               PrimitiveTopology topology);

  std::string m_shaderBaseName;
  VertexLayout m_vertexLayout;
  VkPrimitiveTopology m_vkTopology;
};

} // namespace LX_core::backend
