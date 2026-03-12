#include "vkp_blinnphong.hpp"
#include <stdexcept>

namespace LX_core::graphic_backend {

using VertexType = VertexBlinnPhong;
static VkVertexInputBindingDescription vertexBindingDesc = {
    .binding = 0,
    .stride = sizeof(VertexType),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};
static std::vector<VkVertexInputAttributeDescription> attrDesList = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexType, pos)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexType, normal)},
    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexType, uv)},
    {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexType, color)},
    {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexType, tangent)},
    {5, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(VertexType, boneIDs)},
    {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexType, boneWeights)},
};

VkPipelineVertexInputStateCreateInfo
VulkanPipelineBlinnPhong::getVertexInputStateCreateInfo() {
  return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBindingDesc,
      .vertexAttributeDescriptionCount =
          static_cast<uint32_t>(attrDesList.size()),
      .pVertexAttributeDescriptions = attrDesList.data(),
  };
}

void VulkanPipelineBlinnPhong::initLayoutAndShader() {
  m_descriptorLayouts//???

  VkPipelineLayoutCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.setLayoutCount = static_cast<uint32_t>(m_descriptorLayouts.size());
  info.pSetLayouts = m_descriptorLayouts.data();

  if (vkCreatePipelineLayout(hDevice, &info, nullptr, &hLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout");
  }
}
} // namespace LX_core::graphic_backend
