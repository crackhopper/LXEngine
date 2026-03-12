#pragma once

/**
 * Pipeline 基类
 *
 * 从这个基类，我们会构造若干个通用的pipeline子类。
 *
 * TODO: 重构中....
 */

#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "../vk_device.hpp"

namespace LX_core::graphic_backend {

class VulkanPipelineBase;
using VulkanPipelinePtr = std::unique_ptr<VulkanPipelineBase>;
class VulkanPipelineBase {
protected:
  struct Token {};

public:
  explicit VulkanPipelineBase(Token, VulkanDevice &device, VkExtent2D extent);
  ~VulkanPipelineBase();

  static VulkanPipelinePtr create(VulkanDevice &device, VkExtent2D extent) {
    auto p = std::make_unique<VulkanPipelineBase>(Token{}, device, extent);
    p->initLayoutAndShader();
    return p;
  }

  virtual void initLayoutAndShader() = 0;
  virtual std::string getPipelineId() const = 0;
  virtual VkPipelineVertexInputStateCreateInfo
  getVertexInputStateCreateInfo() = 0;

  virtual VkPipelineInputAssemblyStateCreateInfo
  getInputAssemblyStateCreateInfo();
  virtual VkPipelineShaderStageCreateInfo getVertexShaderStageCreateInfo();
  virtual VkPipelineShaderStageCreateInfo getFragmentShaderStageCreateInfo();
  virtual VkPipelineViewportStateCreateInfo getViewportStateCreateInfo();
  virtual VkPipelineDynamicStateCreateInfo getDynamicStateCreateInfo();
  virtual VkPipelineRasterizationStateCreateInfo getRasterizerStateCreateInfo();
  virtual VkPipelineMultisampleStateCreateInfo getMultisampleStateCreateInfo();
  virtual VkPipelineDepthStencilStateCreateInfo
  getDepthStencilStateCreateInfo();
  virtual VkPipelineColorBlendStateCreateInfo getColorBlendStateCreateInfo();

  VkPipeline buildGraphicsPpl(VkRenderPass renderPass);

  VkPipeline getPipelineHandle() const { return hPipeline; }
  VkPipelineLayout getLayoutHandle() const { return hLayout; }

protected:
  VkDevice hDevice = VK_NULL_HANDLE;
  VkPipeline hPipeline = VK_NULL_HANDLE;
  VkPipelineLayout hLayout = VK_NULL_HANDLE;
  VkShaderModule hVertShader = VK_NULL_HANDLE;
  VkShaderModule hFragShader = VK_NULL_HANDLE;

  // 输出区域
  VkOffset2D offset = {0, 0};
  VkExtent2D extent = {0, 0};

  // 多重采样
  VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
};

} // namespace LX_core::graphic_backend
