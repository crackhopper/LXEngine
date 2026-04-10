#include "vkp_shader_graphics.hpp"
#include "../vk_device.hpp"

namespace LX_core::backend {

namespace {

VkPrimitiveTopology toVkTopology(PrimitiveTopology t) {
  switch (t) {
  case PrimitiveTopology::PointList:
    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  case PrimitiveTopology::LineList:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case PrimitiveTopology::LineStrip:
    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  case PrimitiveTopology::TriangleList:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case PrimitiveTopology::TriangleStrip:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case PrimitiveTopology::TriangleFan:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  default:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  }
}

} // namespace

VulkanPipelinePtr VulkanShaderGraphicsPipeline::create(
    VulkanDevice &device, VkExtent2D extent, std::string shaderBaseName,
    VertexLayout vertexLayout, std::vector<PipelineSlotDetails> slots,
    PushConstantDetails pushConstants, PrimitiveTopology topology) {
  return VulkanPipelinePtr(new VulkanShaderGraphicsPipeline(
      Token{}, device, extent, std::move(shaderBaseName), std::move(vertexLayout),
      std::move(slots), pushConstants, topology));
}

VulkanShaderGraphicsPipeline::VulkanShaderGraphicsPipeline(
    Token t, VulkanDevice &device, VkExtent2D extent, std::string shaderBaseName,
    VertexLayout vertexLayout, std::vector<PipelineSlotDetails> slots,
    PushConstantDetails pushConstants, PrimitiveTopology topology)
    : VulkanPipeline(t, device, extent, shaderBaseName, slots.data(),
                     static_cast<uint32_t>(slots.size()), pushConstants),
      m_shaderBaseName(std::move(shaderBaseName)),
      m_vertexLayout(std::move(vertexLayout)),
      m_vkTopology(toVkTopology(topology)) {}

const VertexLayout &VulkanShaderGraphicsPipeline::referenceVertexLayout() const {
  return m_vertexLayout;
}

std::string VulkanShaderGraphicsPipeline::getShaderName() const {
  return m_shaderBaseName;
}

std::string VulkanShaderGraphicsPipeline::getPipelineId() const {
  return m_shaderBaseName;
}

VkPipelineInputAssemblyStateCreateInfo
VulkanShaderGraphicsPipeline::getInputAssemblyStateCreateInfo() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = m_vkTopology;
  inputAssembly.primitiveRestartEnable = VK_FALSE;
  return inputAssembly;
}

} // namespace LX_core::backend
