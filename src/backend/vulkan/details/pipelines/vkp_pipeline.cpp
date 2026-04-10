#include "vkp_pipeline.hpp"
#include "../vk_device.hpp"
#include "../descriptors/vkd_descriptor_manager.hpp"
#include "core/utils/filesystem_tools.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace LX_core {
namespace backend {

namespace {

VkFormat dataTypeToVkFormat(DataType t) {
  switch (t) {
  case DataType::Float1:
    return VK_FORMAT_R32_SFLOAT;
  case DataType::Float2:
    return VK_FORMAT_R32G32_SFLOAT;
  case DataType::Float3:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case DataType::Float4:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case DataType::Int4:
    return VK_FORMAT_R32G32B32A32_SINT;
  }
  throw std::runtime_error("unhandled DataType for Vulkan vertex input");
}

VkVertexInputRate inputRateToVk(VertexInputRate r) {
  return r == VertexInputRate::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                        : VK_VERTEX_INPUT_RATE_VERTEX;
}

} // namespace

VulkanPipeline::VulkanPipeline(
    Token, VulkanDevice &device, VkExtent2D extent,
    const std::string &shaderName, PipelineSlotDetails *slots,
    uint32_t slotCount, const PushConstantDetails &pushConstants)
    : m_device(device), m_deviceHandle(device.getLogicalDevice()), m_extent(extent),
      m_shaderName(shaderName), m_slots(slots, slots + slotCount),
      m_pushConstants(pushConstants) {}

VulkanPipeline::~VulkanPipeline() {
  if (m_deviceHandle != VK_NULL_HANDLE) {
    if (m_vertShader) vkDestroyShaderModule(m_deviceHandle, m_vertShader, nullptr);
    if (m_fragShader) vkDestroyShaderModule(m_deviceHandle, m_fragShader, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_deviceHandle, m_layout, nullptr);
    if (m_pipeline) vkDestroyPipeline(m_deviceHandle, m_pipeline, nullptr);
  }
}

VkPipelineInputAssemblyStateCreateInfo
VulkanPipeline::getInputAssemblyStateCreateInfo() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;
  return inputAssembly;
}

VkPipelineShaderStageCreateInfo
VulkanPipeline::getVertexShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = m_vertShader;
  vertShaderStageInfo.pName = "main";
  return vertShaderStageInfo;
}

VkPipelineShaderStageCreateInfo
VulkanPipeline::getFragmentShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = m_fragShader;
  fragShaderStageInfo.pName = "main";
  return fragShaderStageInfo;
}

VkPipelineViewportStateCreateInfo
VulkanPipeline::getViewportStateCreateInfo() {
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  m_viewport.x = static_cast<float>(m_offset.x);
  m_viewport.y = static_cast<float>(m_offset.y);
  m_viewport.width = static_cast<float>(m_extent.width);
  m_viewport.height = static_cast<float>(m_extent.height);
  m_viewport.minDepth = 0.0f;
  m_viewport.maxDepth = 1.0f;

  m_scissor.offset = m_offset;
  m_scissor.extent = m_extent;

  viewportState.pViewports = &m_viewport;
  viewportState.pScissors = &m_scissor;
  return viewportState;
}

VkPipelineDynamicStateCreateInfo
VulkanPipeline::getDynamicStateCreateInfo() {
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount =
      static_cast<uint32_t>(m_dynamicStates.size());
  dynamicState.pDynamicStates = m_dynamicStates.data();
  return dynamicState;
}

VkPipelineRasterizationStateCreateInfo
VulkanPipeline::getRasterizerStateCreateInfo() {
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  return rasterizer;
}

VkPipelineMultisampleStateCreateInfo
VulkanPipeline::getMultisampleStateCreateInfo() {
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = m_msaaSamples;
  multisampling.sampleShadingEnable = VK_FALSE;
  return multisampling;
}

VkPipelineDepthStencilStateCreateInfo
VulkanPipeline::getDepthStencilStateCreateInfo() {
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  return depthStencil;
}

VkPipelineColorBlendStateCreateInfo
VulkanPipeline::getColorBlendStateCreateInfo() {
  m_colorBlendAttachment = {};
  m_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &m_colorBlendAttachment;
  return colorBlending;
}

VkPipelineVertexInputStateCreateInfo
VulkanPipeline::getVertexInputStateCreateInfo() {
  const VertexLayout &layout = referenceVertexLayout();
  const auto &items = layout.getItems();

  m_viBindingDescriptions.clear();
  m_viAttrDescriptions.clear();

  if (items.empty() || layout.getStride() == 0) {
    VkPipelineVertexInputStateCreateInfo empty{};
    empty.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    return empty;
  }

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = layout.getStride();
  binding.inputRate = inputRateToVk(items.front().inputRate);
  m_viBindingDescriptions.push_back(binding);

  for (const auto &it : items) {
    VkVertexInputAttributeDescription attr{};
    attr.binding = 0;
    attr.location = it.location;
    attr.format = dataTypeToVkFormat(it.type);
    attr.offset = it.offset;
    m_viAttrDescriptions.push_back(attr);
  }

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<uint32_t>(m_viBindingDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = m_viBindingDescriptions.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(m_viAttrDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = m_viAttrDescriptions.data();
  return vertexInputInfo;
}

VkPipeline VulkanPipeline::buildGraphicsPpl(VkRenderPass renderPass) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0] = getVertexShaderStageCreateInfo();
  stages[1] = getFragmentShaderStageCreateInfo();

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = getVertexInputStateCreateInfo();
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = getInputAssemblyStateCreateInfo();
  VkPipelineViewportStateCreateInfo viewportState = getViewportStateCreateInfo();
  VkPipelineDynamicStateCreateInfo dynamicState = getDynamicStateCreateInfo();
  VkPipelineRasterizationStateCreateInfo rasterizer = getRasterizerStateCreateInfo();
  VkPipelineMultisampleStateCreateInfo multisampling = getMultisampleStateCreateInfo();
  VkPipelineDepthStencilStateCreateInfo depthStencil = getDepthStencilStateCreateInfo();
  VkPipelineColorBlendStateCreateInfo colorBlending = getColorBlendStateCreateInfo();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = m_layout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(m_deviceHandle, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &m_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }
  return m_pipeline;
}

void VulkanPipeline::loadShaders() {
  auto vertCode = readFile(getShaderPath(m_shaderName + ".vert"));
  auto fragCode = readFile(getShaderPath(m_shaderName + ".frag"));

  auto createModule = [&](const std::vector<char> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(m_deviceHandle, &createInfo, nullptr, &module) != VK_SUCCESS) {
      throw std::runtime_error("failed to create shader module!");
    }
    return module;
  };

  m_vertShader = createModule(vertCode);
  m_fragShader = createModule(fragCode);
}

void VulkanPipeline::createLayout() {
  auto &descriptorMgr = m_device.getDescriptorManager();

  std::unordered_map<uint32_t, std::vector<PipelineSlotDetails>> setGroups;
  for (const auto &slot : m_slots) {
    setGroups[slot.setIndex].push_back(slot);
  }

  std::vector<VkDescriptorSetLayout> setLayouts;
  for (uint32_t i = 0; i < setGroups.size(); ++i) {
    setLayouts.push_back(descriptorMgr.getOrCreateLayout(setGroups[i]));
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  VkPushConstantRange range{};
  if (m_pushConstants.size > 0) {
    range.stageFlags = m_pushConstants.stageFlags;
    range.offset = m_pushConstants.offset;
    range.size = m_pushConstants.size;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &range;
  }

  if (vkCreatePipelineLayout(m_deviceHandle, &pipelineLayoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

} // namespace backend
} // namespace LX_core
