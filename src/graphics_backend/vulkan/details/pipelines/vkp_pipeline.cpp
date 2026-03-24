#include "vkp_pipeline.hpp"
#include "../vk_device.hpp"
#include "../descriptors/vkd_descriptor_manager.hpp"
#include <fstream>

namespace LX_core {
namespace graphic_backend {

static std::vector<char> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open file: " + filename);
  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();
  return buffer;
}

VulkanPipelineBase::VulkanPipelineBase(
    Token, VulkanDevice &device, VkExtent2D extent,
    const std::string &shaderName, PipelineSlotDetails *slots,
    uint32_t slotCount, const PushConstantDetails &pushConstants)
    : m_device(&device), m_deviceHandle(device.getLogicalDevice()), m_extent(extent),
      m_shaderName(shaderName), m_slots(slots, slots + slotCount),
      m_pushConstants(pushConstants) {}

VulkanPipelineBase::~VulkanPipelineBase() {
  if (m_deviceHandle != VK_NULL_HANDLE) {
    if (m_vertShader) vkDestroyShaderModule(m_deviceHandle, m_vertShader, nullptr);
    if (m_fragShader) vkDestroyShaderModule(m_deviceHandle, m_fragShader, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_deviceHandle, m_layout, nullptr);
    if (m_pipeline) vkDestroyPipeline(m_deviceHandle, m_pipeline, nullptr);
  }
}

VkPipelineInputAssemblyStateCreateInfo
VulkanPipelineBase::getInputAssemblyStateCreateInfo() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;
  return inputAssembly;
}

VkPipelineShaderStageCreateInfo
VulkanPipelineBase::getVertexShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = m_vertShader;
  vertShaderStageInfo.pName = "main";
  return vertShaderStageInfo;
}

VkPipelineShaderStageCreateInfo
VulkanPipelineBase::getFragmentShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = m_fragShader;
  fragShaderStageInfo.pName = "main";
  return fragShaderStageInfo;
}

VkPipelineViewportStateCreateInfo
VulkanPipelineBase::getViewportStateCreateInfo() {
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Use members to keep pointers valid until vkCreateGraphicsPipelines returns.
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
VulkanPipelineBase::getDynamicStateCreateInfo() {
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount =
      static_cast<uint32_t>(m_dynamicStates.size());
  dynamicState.pDynamicStates = m_dynamicStates.data();
  return dynamicState;
}

VkPipelineRasterizationStateCreateInfo
VulkanPipelineBase::getRasterizerStateCreateInfo() {
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
VulkanPipelineBase::getMultisampleStateCreateInfo() {
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = m_msaaSamples;
  multisampling.sampleShadingEnable = VK_FALSE;
  return multisampling;
}

VkPipelineDepthStencilStateCreateInfo
VulkanPipelineBase::getDepthStencilStateCreateInfo() {
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  return depthStencil;
}

VkPipelineColorBlendStateCreateInfo
VulkanPipelineBase::getColorBlendStateCreateInfo() {
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  return colorBlending;
}

VkPipelineVertexInputStateCreateInfo
VulkanPipelineBase::getVertexInputStateCreateInfo() {
  VertexFormat format = getVertexFormat();

  m_viBindingDescriptions.clear();
  m_viAttrDescriptions.clear();

  if (format == VertexFormat::Custom) {
    VkPipelineVertexInputStateCreateInfo empty{};
    empty.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    return empty;
  }

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  uint32_t stride = 0;

  auto addAttr = [&](uint32_t loc, VkFormat vkFormat, uint32_t offset) {
    VkVertexInputAttributeDescription attr{};
    attr.binding = 0;
    attr.location = loc;
    attr.format = vkFormat;
    attr.offset = offset;
    m_viAttrDescriptions.push_back(attr);
  };

  switch (format) {
  case VertexFormat::Pos:
    stride = sizeof(VertexPos);
    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPos, pos));
    break;
  case VertexFormat::PosColor:
    stride = sizeof(VertexPosColor);
    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosColor, pos));
    addAttr(1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosColor, color));
    break;
  case VertexFormat::PosUV:
    stride = sizeof(VertexPosUV);
    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosUV, pos));
    addAttr(1, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosUV, uv));
    break;
  case VertexFormat::NormalTangent:
    stride = sizeof(VertexNormalTangent);
    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexNormalTangent, normal));
    addAttr(1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexNormalTangent, tangent));
    break;
  case VertexFormat::BoneWeight:
    stride = sizeof(VertexBoneWeight);
    addAttr(0, VK_FORMAT_R32G32B32A32_SINT, offsetof(VertexBoneWeight, boneIds));
    addAttr(1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexBoneWeight, weights));
    break;
  case VertexFormat::PosNormalUvBone:
    stride = sizeof(VertexPosNormalUvBone);
    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosNormalUvBone, pos));
    addAttr(1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosNormalUvBone, normal));
    addAttr(2, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosNormalUvBone, uv));
    addAttr(3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexPosNormalUvBone, tangent));
    addAttr(4, VK_FORMAT_R32G32B32A32_SINT, offsetof(VertexPosNormalUvBone, boneIDs));
    addAttr(5, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexPosNormalUvBone, boneWeights));
    break;
  }

  binding.stride = stride;
  m_viBindingDescriptions.push_back(binding);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_viBindingDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = m_viBindingDescriptions.data();
  vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_viAttrDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = m_viAttrDescriptions.data();
  return vertexInputInfo;
}

VkPipeline VulkanPipelineBase::buildGraphicsPpl(VkRenderPass renderPass) {
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

void VulkanPipelineBase::loadShaders() {
  auto vertCode = readFile("shaders/glsl/" + m_shaderName + ".vert.spv");
  auto fragCode = readFile("shaders/glsl/" + m_shaderName + ".frag.spv");

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

void VulkanPipelineBase::createLayout() {
  auto &descriptorMgr = m_device->getDescriptorManager();

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

  if (m_pushConstants.size > 0) {
    VkPushConstantRange range{};
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

} // namespace graphic_backend
} // namespace LX_core
