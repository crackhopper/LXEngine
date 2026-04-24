#include "pipeline.hpp"
#include "../descriptors/descriptor_manager.hpp"
#include "../device.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

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

VkPrimitiveTopology topologyToVk(PrimitiveTopology t) {
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
  }
  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags cullToVk(CullMode c) {
  switch (c) {
  case CullMode::None:
    return VK_CULL_MODE_NONE;
  case CullMode::Front:
    return VK_CULL_MODE_FRONT_BIT;
  case CullMode::Back:
    return VK_CULL_MODE_BACK_BIT;
  }
  return VK_CULL_MODE_BACK_BIT;
}

VkCompareOp compareOpToVk(CompareOp op) {
  switch (op) {
  case CompareOp::Less:
    return VK_COMPARE_OP_LESS;
  case CompareOp::LessEqual:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case CompareOp::Greater:
    return VK_COMPARE_OP_GREATER;
  case CompareOp::Equal:
    return VK_COMPARE_OP_EQUAL;
  case CompareOp::Always:
    return VK_COMPARE_OP_ALWAYS;
  }
  return VK_COMPARE_OP_LESS_OR_EQUAL;
}

VkBlendFactor blendFactorToVk(BlendFactor f) {
  switch (f) {
  case BlendFactor::Zero:
    return VK_BLEND_FACTOR_ZERO;
  case BlendFactor::One:
    return VK_BLEND_FACTOR_ONE;
  case BlendFactor::SrcAlpha:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case BlendFactor::OneMinusSrcAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }
  return VK_BLEND_FACTOR_ONE;
}

VkShaderStageFlags shaderStageMaskToVk(ShaderStage mask) {
  VkShaderStageFlags out = 0;
  const auto m = static_cast<ShaderStageMask32>(mask);
  if (m & static_cast<ShaderStageMask32>(ShaderStage::Vertex))
    out |= VK_SHADER_STAGE_VERTEX_BIT;
  if (m & static_cast<ShaderStageMask32>(ShaderStage::Fragment))
    out |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (m & static_cast<ShaderStageMask32>(ShaderStage::Compute))
    out |= VK_SHADER_STAGE_COMPUTE_BIT;
  if (m & static_cast<ShaderStageMask32>(ShaderStage::Geometry))
    out |= VK_SHADER_STAGE_GEOMETRY_BIT;
  if (m & static_cast<ShaderStageMask32>(ShaderStage::TessControl))
    out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
  if (m & static_cast<ShaderStageMask32>(ShaderStage::TessEval))
    out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
  return out;
}

VkShaderStageFlags pushConstantStageMaskToVk(ShaderStageMask32 mask) {
  return shaderStageMaskToVk(static_cast<ShaderStage>(mask));
}

} // namespace

// Publicly-visible helpers reused by the descriptor manager and command buffer
// modules. Defined here so the conversion tables have a single source of truth.
VkDescriptorType toVkDescriptorType(ShaderPropertyType t) {
  switch (t) {
  case ShaderPropertyType::UniformBuffer:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case ShaderPropertyType::StorageBuffer:
    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  case ShaderPropertyType::Texture2D:
  case ShaderPropertyType::TextureCube:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  case ShaderPropertyType::Sampler:
    return VK_DESCRIPTOR_TYPE_SAMPLER;
  default:
    throw std::runtime_error("toVkDescriptorType: non-descriptor type");
  }
}

VkShaderStageFlags toVkShaderStageFlags(ShaderStage mask) {
  return shaderStageMaskToVk(mask);
}

VulkanPipeline::VulkanPipeline(Token, VulkanDevice &device,
                               const PipelineBuildDesc &buildInfo)
    : m_device(device), m_deviceHandle(device.getLogicalDevice()),
      m_stages(buildInfo.stages), m_bindings(buildInfo.bindings),
      m_vertexLayout(buildInfo.vertexLayout),
      m_renderState(buildInfo.renderState), m_topology(buildInfo.topology),
      m_pushConstant(buildInfo.pushConstant) {}

VulkanPipeline::~VulkanPipeline() {
  if (m_deviceHandle != VK_NULL_HANDLE) {
    if (m_vertShader)
      vkDestroyShaderModule(m_deviceHandle, m_vertShader, nullptr);
    if (m_fragShader)
      vkDestroyShaderModule(m_deviceHandle, m_fragShader, nullptr);
    if (m_layout)
      vkDestroyPipelineLayout(m_deviceHandle, m_layout, nullptr);
    if (m_pipeline)
      vkDestroyPipeline(m_deviceHandle, m_pipeline, nullptr);
  }
}

VkPipelineInputAssemblyStateCreateInfo
VulkanPipeline::getInputAssemblyStateCreateInfo() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = topologyToVk(m_topology);
  inputAssembly.primitiveRestartEnable = VK_FALSE;
  return inputAssembly;
}

VkPipelineShaderStageCreateInfo
VulkanPipeline::getVertexShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = m_vertShader;
  vertShaderStageInfo.pName = "main";
  return vertShaderStageInfo;
}

VkPipelineShaderStageCreateInfo
VulkanPipeline::getFragmentShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = m_fragShader;
  fragShaderStageInfo.pName = "main";
  return fragShaderStageInfo;
}

VkPipelineViewportStateCreateInfo VulkanPipeline::getViewportStateCreateInfo() {
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

VkPipelineDynamicStateCreateInfo VulkanPipeline::getDynamicStateCreateInfo() {
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<u32>(m_dynamicStates.size());
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
  rasterizer.cullMode = cullToVk(m_renderState.cullMode);
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  return rasterizer;
}

VkPipelineMultisampleStateCreateInfo
VulkanPipeline::getMultisampleStateCreateInfo() {
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = m_msaaSamples;
  multisampling.sampleShadingEnable = VK_FALSE;
  return multisampling;
}

VkPipelineDepthStencilStateCreateInfo
VulkanPipeline::getDepthStencilStateCreateInfo() {
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable =
      m_renderState.depthTestEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable =
      m_renderState.depthWriteEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = compareOpToVk(m_renderState.depthOp);
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  return depthStencil;
}

VkPipelineColorBlendStateCreateInfo
VulkanPipeline::getColorBlendStateCreateInfo() {
  m_colorBlendAttachment = {};
  m_colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_colorBlendAttachment.blendEnable =
      m_renderState.blendEnable ? VK_TRUE : VK_FALSE;
  m_colorBlendAttachment.srcColorBlendFactor =
      blendFactorToVk(m_renderState.srcBlend);
  m_colorBlendAttachment.dstColorBlendFactor =
      blendFactorToVk(m_renderState.dstBlend);
  m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  m_colorBlendAttachment.srcAlphaBlendFactor =
      blendFactorToVk(m_renderState.srcBlend);
  m_colorBlendAttachment.dstAlphaBlendFactor =
      blendFactorToVk(m_renderState.dstBlend);
  m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &m_colorBlendAttachment;
  return colorBlending;
}

VkPipelineVertexInputStateCreateInfo
VulkanPipeline::getVertexInputStateCreateInfo() {
  const VertexLayout &layout = m_vertexLayout;
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
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<u32>(m_viBindingDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = m_viBindingDescriptions.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<u32>(m_viAttrDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = m_viAttrDescriptions.data();
  return vertexInputInfo;
}

VkPipeline VulkanPipeline::buildGraphicsPpl(VkRenderPass renderPass) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0] = getVertexShaderStageCreateInfo();
  stages[1] = getFragmentShaderStageCreateInfo();

  VkPipelineVertexInputStateCreateInfo vertexInputInfo =
      getVertexInputStateCreateInfo();
  VkPipelineInputAssemblyStateCreateInfo inputAssembly =
      getInputAssemblyStateCreateInfo();
  VkPipelineViewportStateCreateInfo viewportState =
      getViewportStateCreateInfo();
  VkPipelineDynamicStateCreateInfo dynamicState = getDynamicStateCreateInfo();
  VkPipelineRasterizationStateCreateInfo rasterizer =
      getRasterizerStateCreateInfo();
  VkPipelineMultisampleStateCreateInfo multisampling =
      getMultisampleStateCreateInfo();
  VkPipelineDepthStencilStateCreateInfo depthStencil =
      getDepthStencilStateCreateInfo();
  VkPipelineColorBlendStateCreateInfo colorBlending =
      getColorBlendStateCreateInfo();

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

  if (vkCreateGraphicsPipelines(m_deviceHandle, VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &m_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }
  return m_pipeline;
}

void VulkanPipeline::loadShaders() {
  auto createModule = [&](const std::vector<u32> &bytecode) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = bytecode.size() * sizeof(u32);
    createInfo.pCode = bytecode.data();
    VkShaderModule module;
    if (vkCreateShaderModule(m_deviceHandle, &createInfo, nullptr, &module) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create shader module!");
    }
    return module;
  };

  for (const auto &stage : m_stages) {
    if (stage.stage == ShaderStage::Vertex) {
      m_vertShader = createModule(stage.bytecode);
    } else if (stage.stage == ShaderStage::Fragment) {
      m_fragShader = createModule(stage.bytecode);
    }
  }
}

void VulkanPipeline::createLayout() {
  auto &descriptorMgr = m_device.getDescriptorManager();

  std::unordered_map<DescriptorSetIndex32,
                     std::vector<LX_core::ShaderResourceBinding>>
      setGroups;
  for (const auto &b : m_bindings) {
    setGroups[b.set].push_back(b);
  }

  DescriptorSetIndex32 maxSet = 0;
  for (const auto &kv : setGroups)
    maxSet = std::max(maxSet, kv.first);

  std::vector<VkDescriptorSetLayout> setLayouts(
      setGroups.empty() ? 0 : (maxSet + 1), VK_NULL_HANDLE);
  for (auto &[setIdx, group] : setGroups) {
    setLayouts[setIdx] = descriptorMgr.getOrCreateLayout(group);
  }
  // Fill gap sets (declared N but only some indices used) with empty layouts.
  VkDescriptorSetLayout emptyLayout = VK_NULL_HANDLE;
  for (auto &l : setLayouts) {
    if (l == VK_NULL_HANDLE) {
      if (emptyLayout == VK_NULL_HANDLE) {
        emptyLayout = descriptorMgr.getOrCreateLayout({});
      }
      l = emptyLayout;
    }
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  VkPushConstantRange range{};
  if (m_pushConstant.size > 0) {
    range.stageFlags = pushConstantStageMaskToVk(m_pushConstant.stageFlagsMask);
    range.offset = m_pushConstant.offset;
    range.size = m_pushConstant.size;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &range;
  }

  if (vkCreatePipelineLayout(m_deviceHandle, &pipelineLayoutInfo, nullptr,
                             &m_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

} // namespace backend
} // namespace LX_core
