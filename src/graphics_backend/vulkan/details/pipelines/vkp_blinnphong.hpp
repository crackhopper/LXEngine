#pragma once
#include "core/resources/vertex.hpp"
#include "vkp_base.hpp"
#include <vector>
#include <vulkan/vulkan.h>
// 无光照管线

namespace LX_core::graphic_backend {

class VulkanPipelineBlinnPhong : public VulkanPipelineBase {
  using VertexType = VertexBlinnPhong;
public:
  VulkanPipelineBlinnPhong(Token t, VulkanDevice &device, VkExtent2D extent): VulkanPipelineBase(t, device, extent) {}

  static VulkanPipelinePtr create(VulkanDevice &device, VkExtent2D extent) {
    auto p = std::make_unique<VulkanPipelineBlinnPhong>(Token{}, device, extent);
    p->initLayoutAndShader();
    return p;
  }

  void initLayoutAndShader() override;
  VkPipelineVertexInputStateCreateInfo getVertexInputStateCreateInfo() override;

  std::string getPipelineId() const override { return pipelineId; }

private:
  std::string pipelineId = "blinnphong_0";
  std::vector<VkDescriptorSetLayout> m_descriptorLayouts;
};
} // namespace LX_core::graphic_backend
