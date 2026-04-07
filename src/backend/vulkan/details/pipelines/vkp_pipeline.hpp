#pragma once

/**
 * Pipeline 基类
 *
 * 从这个基类，我们会构造若干个通用的pipeline子类。
 *
 */

#include "core/resources/vertex_buffer.hpp"
#include "vkp_pipeline_slot.hpp"
#include <memory>
#include <array>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

class VulkanDevice;

struct PushConstantDetails {
  VkShaderStageFlags stageFlags;
  uint32_t offset;
  uint32_t size;
};

class VulkanPipeline;
using VulkanPipelinePtr = std::unique_ptr<VulkanPipeline>;
class VulkanPipeline {
protected:
  struct Token {};

public:
  explicit VulkanPipeline(Token, VulkanDevice &device, VkExtent2D extent,
                             const std::string &shaderName,
                             PipelineSlotDetails *slots, uint32_t slotCount,
                             const PushConstantDetails &pushConstants);
  virtual ~VulkanPipeline();

  virtual std::string getPipelineId() const = 0;
  virtual std::string getShaderName() const = 0;
  virtual VertexFormat getVertexFormat() const = 0;

  virtual void loadShaders();
  virtual void createLayout();
  virtual VkPipelineVertexInputStateCreateInfo getVertexInputStateCreateInfo();
  virtual VkPipelineInputAssemblyStateCreateInfo getInputAssemblyStateCreateInfo();
  virtual VkPipelineShaderStageCreateInfo getVertexShaderStageCreateInfo();
  virtual VkPipelineShaderStageCreateInfo getFragmentShaderStageCreateInfo();
  virtual VkPipelineViewportStateCreateInfo getViewportStateCreateInfo();
  virtual VkPipelineDynamicStateCreateInfo getDynamicStateCreateInfo();
  virtual VkPipelineRasterizationStateCreateInfo getRasterizerStateCreateInfo();
  virtual VkPipelineMultisampleStateCreateInfo getMultisampleStateCreateInfo();
  virtual VkPipelineDepthStencilStateCreateInfo getDepthStencilStateCreateInfo();
  virtual VkPipelineColorBlendStateCreateInfo getColorBlendStateCreateInfo();

  VkPipeline buildGraphicsPpl(VkRenderPass renderPass);

  VkPipeline getHandle() const { return m_pipeline; }
  VkPipelineLayout getLayout() const { return m_layout; }

  const std::vector<PipelineSlotDetails> &getSlots() const { return m_slots; }
  const PushConstantDetails &getPushConstantDetails() const {
    return m_pushConstants;
  }

protected:
  VulkanDevice &m_device;
  VkDevice m_deviceHandle = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkPipelineLayout m_layout = VK_NULL_HANDLE;

  std::string m_shaderName;
  std::vector<PipelineSlotDetails> m_slots;
  PushConstantDetails m_pushConstants;

  VkShaderModule m_vertShader = VK_NULL_HANDLE;
  VkShaderModule m_fragShader = VK_NULL_HANDLE;

  VkOffset2D m_offset = {0, 0};
  VkExtent2D m_extent = {0, 0};

  // Stored to keep pointer members valid during vkCreateGraphicsPipelines.
  VkViewport m_viewport{};
  VkRect2D m_scissor{};
  VkPipelineColorBlendAttachmentState m_colorBlendAttachment{};
  std::array<VkDynamicState, 2> m_dynamicStates{
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;

  std::vector<VkVertexInputBindingDescription> m_viBindingDescriptions;
  std::vector<VkVertexInputAttributeDescription> m_viAttrDescriptions;
};

} // namespace backend
} // namespace LX_core
