#pragma once

/**
 * VulkanPipeline — reflection-driven graphics pipeline.
 *
 * Consumes a `LX_core::PipelineBuildDesc` end-to-end: SPIR-V bytecode from
 * `stages`, descriptor set layouts from `bindings`, vertex input from
 * `vertexLayout`, fixed-function state from `renderState`, topology from
 * `topology`, and push constant range from `pushConstant`.
 */

#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace LX_core {
namespace backend {

class VulkanDevice;

class VulkanPipeline;
using VulkanPipelineUniquePtr = std::unique_ptr<VulkanPipeline>;

class VulkanPipeline {
protected:
  struct Token {};

public:
  explicit VulkanPipeline(Token, VulkanDevice &device,
                          const PipelineBuildDesc &buildInfo);
  virtual ~VulkanPipeline();

  virtual std::string getPipelineId() const { return {}; }
  virtual std::string getShaderName() const { return {}; }
  /// Vertex input reference (forwards to stored buildInfo layout).
  virtual const VertexLayout &referenceVertexLayout() const {
    return m_vertexLayout;
  }

  void loadShaders();
  void createLayout();

  VkPipelineVertexInputStateCreateInfo getVertexInputStateCreateInfo();
  VkPipelineInputAssemblyStateCreateInfo getInputAssemblyStateCreateInfo();
  VkPipelineShaderStageCreateInfo getVertexShaderStageCreateInfo();
  VkPipelineShaderStageCreateInfo getFragmentShaderStageCreateInfo();
  VkPipelineViewportStateCreateInfo getViewportStateCreateInfo();
  VkPipelineDynamicStateCreateInfo getDynamicStateCreateInfo();
  VkPipelineRasterizationStateCreateInfo getRasterizerStateCreateInfo();
  VkPipelineMultisampleStateCreateInfo getMultisampleStateCreateInfo();
  VkPipelineDepthStencilStateCreateInfo getDepthStencilStateCreateInfo();
  VkPipelineColorBlendStateCreateInfo getColorBlendStateCreateInfo();

  VkPipeline buildGraphicsPpl(VkRenderPass renderPass);

  VkPipeline getHandle() const { return m_pipeline; }
  VkPipelineLayout getLayout() const { return m_layout; }

  const std::vector<LX_core::ShaderResourceBinding> &getBindings() const {
    return m_bindings;
  }
  const PushConstantRange &getPushConstantRange() const {
    return m_pushConstant;
  }

protected:
  VulkanDevice &m_device;
  VkDevice m_deviceHandle = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkPipelineLayout m_layout = VK_NULL_HANDLE;

  // Data pulled from PipelineBuildDesc in the constructor.
  std::vector<LX_core::ShaderStageCode> m_stages;
  std::vector<LX_core::ShaderResourceBinding> m_bindings;
  VertexLayout m_vertexLayout;
  RenderState m_renderState;
  PrimitiveTopology m_topology = PrimitiveTopology::TriangleList;
  PushConstantRange m_pushConstant;

  VkShaderModule m_vertShader = VK_NULL_HANDLE;
  VkShaderModule m_fragShader = VK_NULL_HANDLE;

  VkOffset2D m_offset = {0, 0};
  VkExtent2D m_extent = {1, 1}; // dynamic, overridden at cmd-buffer time

  VkViewport m_viewport{};
  VkRect2D m_scissor{};
  VkPipelineColorBlendAttachmentState m_colorBlendAttachment{};
  std::array<VkDynamicState, 2> m_dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};

  VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;

  std::vector<VkVertexInputBindingDescription> m_viBindingDescriptions;
  std::vector<VkVertexInputAttributeDescription> m_viAttrDescriptions;
};

} // namespace backend
} // namespace LX_core
