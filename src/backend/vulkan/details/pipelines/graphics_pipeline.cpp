#include "graphics_pipeline.hpp"
#include "core/utils/env.hpp"
#include "../descriptors/descriptor_manager.hpp"
#include "../device.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
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

VkFormat imageFormatToVk(ImageFormat format) {
  switch (format) {
  case ImageFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case ImageFormat::RGBA8Srgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case ImageFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case ImageFormat::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case ImageFormat::BGRA8Srgb:
    return VK_FORMAT_B8G8R8A8_SRGB;
  case ImageFormat::R8:
    return VK_FORMAT_R8_UNORM;
  case ImageFormat::D32Float:
    return VK_FORMAT_D32_SFLOAT;
  case ImageFormat::D24UnormS8:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case ImageFormat::D32FloatS8:
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  }
  throw std::runtime_error("Unsupported ImageFormat for graphics pipeline");
}

VkSampleCountFlagBits samplesToVk(u32 samples) {
  switch (samples) {
  case 1:
    return VK_SAMPLE_COUNT_1_BIT;
  case 2:
    return VK_SAMPLE_COUNT_2_BIT;
  case 4:
    return VK_SAMPLE_COUNT_4_BIT;
  case 8:
    return VK_SAMPLE_COUNT_8_BIT;
  }
  throw std::runtime_error("Unsupported render path attachment sample count: " +
                           std::to_string(samples));
}

bool isStencilFormat(VkFormat format) {
  return format == VK_FORMAT_D24_UNORM_S8_UINT ||
         format == VK_FORMAT_D32_SFLOAT_S8_UINT;
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

const char *shaderStageName(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::None:
    return "None";
  case ShaderStage::Vertex:
    return "Vertex";
  case ShaderStage::Fragment:
    return "Fragment";
  case ShaderStage::Compute:
    return "Compute";
  case ShaderStage::Geometry:
    return "Geometry";
  case ShaderStage::TessControl:
    return "TessControl";
  case ShaderStage::TessEval:
    return "TessEval";
  }
  return "Unknown";
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

VulkanGraphicsPipeline::VulkanGraphicsPipeline(
    Token, VulkanDevice &device, const PipelineBuildDesc &buildInfo)
    : m_device(device), m_deviceHandle(device.getLogicalDevice()),
      m_stages(buildInfo.stages), m_bindings(buildInfo.bindings),
      m_specializationConstants(buildInfo.specializationConstants),
      m_vertexLayout(buildInfo.vertexLayout), m_target(buildInfo.target),
      m_renderingMode(buildInfo.renderingMode),
      m_attachments(buildInfo.attachments),
      m_renderState(buildInfo.renderState), m_topology(buildInfo.topology),
      m_pushConstant(buildInfo.pushConstant) {
  buildSpecializationInfos();

  if (!m_attachments.empty()) {
    const u32 expectedSamples = m_attachments.front().samples;
    const u32 expectedLayers = m_attachments.front().layers;
    for (const auto &attachment : m_attachments) {
      if (attachment.samples != expectedSamples) {
        throw std::runtime_error(
            "Render path attachment sample count mismatch in one pipeline");
      }
      if (attachment.layers != expectedLayers) {
        throw std::runtime_error(
            "Render path attachment layer count mismatch in one pipeline");
      }
    }
    m_msaaSamples = samplesToVk(expectedSamples);
  }
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
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
VulkanGraphicsPipeline::getInputAssemblyStateCreateInfo() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = topologyToVk(m_topology);
  inputAssembly.primitiveRestartEnable = VK_FALSE;
  return inputAssembly;
}

VkPipelineShaderStageCreateInfo
VulkanGraphicsPipeline::getVertexShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = m_vertShader;
  vertShaderStageInfo.pName = "main";
  if (!m_vertexSpecializationMapEntries.empty()) {
    vertShaderStageInfo.pSpecializationInfo = &m_vertexSpecializationInfo;
  }
  return vertShaderStageInfo;
}

VkPipelineShaderStageCreateInfo
VulkanGraphicsPipeline::getFragmentShaderStageCreateInfo() {
  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = m_fragShader;
  fragShaderStageInfo.pName = "main";
  if (!m_fragmentSpecializationMapEntries.empty()) {
    fragShaderStageInfo.pSpecializationInfo = &m_fragmentSpecializationInfo;
  }
  return fragShaderStageInfo;
}

VkPipelineViewportStateCreateInfo
VulkanGraphicsPipeline::getViewportStateCreateInfo() {
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
VulkanGraphicsPipeline::getDynamicStateCreateInfo() {
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<u32>(m_dynamicStates.size());
  dynamicState.pDynamicStates = m_dynamicStates.data();
  return dynamicState;
}

VkPipelineRasterizationStateCreateInfo
VulkanGraphicsPipeline::getRasterizerStateCreateInfo() {
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = expEnvEnabled("LX_RENDER_DISABLE_CULL")
                            ? VK_CULL_MODE_NONE
                            : cullToVk(m_renderState.cullMode);
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  return rasterizer;
}

VkPipelineMultisampleStateCreateInfo
VulkanGraphicsPipeline::getMultisampleStateCreateInfo() {
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = m_msaaSamples;
  multisampling.sampleShadingEnable = VK_FALSE;
  return multisampling;
}

VkPipelineDepthStencilStateCreateInfo
VulkanGraphicsPipeline::getDepthStencilStateCreateInfo() {
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  const bool disableDepth = expEnvEnabled("LX_RENDER_DISABLE_DEPTH");
  depthStencil.depthTestEnable =
      !disableDepth && m_renderState.depthTestEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable =
      !disableDepth && m_renderState.depthWriteEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = disableDepth
                                    ? VK_COMPARE_OP_ALWAYS
                                    : compareOpToVk(m_renderState.depthOp);
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  return depthStencil;
}

VkPipelineColorBlendStateCreateInfo
VulkanGraphicsPipeline::getColorBlendStateCreateInfo() {
  VkPipelineColorBlendAttachmentState attachment{};
  attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  attachment.blendEnable = m_renderState.blendEnable ? VK_TRUE : VK_FALSE;
  attachment.srcColorBlendFactor = blendFactorToVk(m_renderState.srcBlend);
  attachment.dstColorBlendFactor = blendFactorToVk(m_renderState.dstBlend);
  attachment.colorBlendOp = VK_BLEND_OP_ADD;
  attachment.srcAlphaBlendFactor = blendFactorToVk(m_renderState.srcBlend);
  attachment.dstAlphaBlendFactor = blendFactorToVk(m_renderState.dstBlend);
  attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  usize colorAttachmentCount = m_target.colorAttachmentCount();
  if (!m_attachments.empty()) {
    colorAttachmentCount = static_cast<usize>(
        std::count_if(m_attachments.begin(), m_attachments.end(),
                      [](const RenderPathAttachmentContract &contract) {
                        return !contract.depth;
                      }));
  }
  m_colorBlendAttachments.assign(colorAttachmentCount, attachment);

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount =
      static_cast<u32>(m_colorBlendAttachments.size());
  colorBlending.pAttachments = m_colorBlendAttachments.empty()
                                   ? nullptr
                                   : m_colorBlendAttachments.data();
  return colorBlending;
}

VkPipelineVertexInputStateCreateInfo
VulkanGraphicsPipeline::getVertexInputStateCreateInfo() {
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

void VulkanGraphicsPipeline::buildSpecializationInfos() {
  auto appendConstant =
      [](const ShaderSpecializationConstant &constant,
         std::vector<VkSpecializationMapEntry> &entries,
         std::vector<u8> &data) {
        VkSpecializationMapEntry entry{};
        entry.constantID = constant.constantId;
        entry.offset = static_cast<u32>(data.size());
        entry.size = sizeof(u32);
        entries.push_back(entry);

        const u32 value = constant.valueU32;
        const auto *bytes = reinterpret_cast<const u8 *>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(value));
      };

  for (const ShaderSpecializationConstant &constant :
       m_specializationConstants) {
    switch (constant.stage) {
    case ShaderStage::Vertex:
      appendConstant(constant, m_vertexSpecializationMapEntries,
                     m_vertexSpecializationData);
      break;
    case ShaderStage::Fragment:
      appendConstant(constant, m_fragmentSpecializationMapEntries,
                     m_fragmentSpecializationData);
      break;
    default:
      throw std::runtime_error(
          "Vulkan graphics pipeline only supports Vertex and Fragment "
          "specialization constants; constant " +
          std::to_string(constant.constantId) + " requested " +
          shaderStageName(constant.stage));
    }
  }

  if (!m_vertexSpecializationMapEntries.empty()) {
    m_vertexSpecializationInfo.mapEntryCount =
        static_cast<u32>(m_vertexSpecializationMapEntries.size());
    m_vertexSpecializationInfo.pMapEntries =
        m_vertexSpecializationMapEntries.data();
    m_vertexSpecializationInfo.dataSize = m_vertexSpecializationData.size();
    m_vertexSpecializationInfo.pData = m_vertexSpecializationData.data();
  }

  if (!m_fragmentSpecializationMapEntries.empty()) {
    m_fragmentSpecializationInfo.mapEntryCount =
        static_cast<u32>(m_fragmentSpecializationMapEntries.size());
    m_fragmentSpecializationInfo.pMapEntries =
        m_fragmentSpecializationMapEntries.data();
    m_fragmentSpecializationInfo.dataSize =
        m_fragmentSpecializationData.size();
    m_fragmentSpecializationInfo.pData = m_fragmentSpecializationData.data();
  }
}

VkPipeline VulkanGraphicsPipeline::buildGraphicsPpl(VkRenderPass renderPass) {
  const bool useDynamicRendering =
      m_renderingMode.has_value() &&
      *m_renderingMode == RenderPathNodeRenderingMode::Dynamic;
  if (useDynamicRendering && !m_device.supportsDynamicRendering()) {
    throw std::runtime_error(
        "RenderPathNode requested dynamic rendering but Vulkan device did not "
        "enable dynamicRendering");
  }
  if (!useDynamicRendering && renderPass == VK_NULL_HANDLE) {
    throw std::runtime_error(
        "Traditional graphics pipeline requires a non-null VkRenderPass");
  }
  if (useDynamicRendering && m_attachments.empty()) {
    throw std::runtime_error(
        "Dynamic graphics pipeline requires explicit RenderPathNode "
        "attachment contracts");
  }

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

  std::vector<VkFormat> dynamicColorFormats;
  std::optional<VkFormat> dynamicDepthFormat;
  std::optional<VkFormat> dynamicStencilFormat;
  VkPipelineRenderingCreateInfo dynamicRenderingInfo{};
  if (useDynamicRendering) {
    for (const auto &attachment : m_attachments) {
      const VkFormat format = imageFormatToVk(attachment.format);
      if (attachment.depth) {
        if (dynamicDepthFormat.has_value()) {
          throw std::runtime_error(
              "Dynamic graphics pipeline received multiple depth "
              "attachments");
        }
        dynamicDepthFormat = format;
        if (isStencilFormat(format)) {
          dynamicStencilFormat = format;
        }
      } else {
        dynamicColorFormats.push_back(format);
      }
    }

    dynamicRenderingInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    dynamicRenderingInfo.colorAttachmentCount =
        static_cast<u32>(dynamicColorFormats.size());
    dynamicRenderingInfo.pColorAttachmentFormats =
        dynamicColorFormats.empty() ? nullptr : dynamicColorFormats.data();
    dynamicRenderingInfo.depthAttachmentFormat =
        dynamicDepthFormat.value_or(VK_FORMAT_UNDEFINED);
    dynamicRenderingInfo.stencilAttachmentFormat =
        dynamicStencilFormat.value_or(VK_FORMAT_UNDEFINED);
  }

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
  if (useDynamicRendering) {
    pipelineInfo.pNext = &dynamicRenderingInfo;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
  } else {
    pipelineInfo.renderPass = renderPass;
  }
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(m_deviceHandle, VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &m_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }
  return m_pipeline;
}

void VulkanGraphicsPipeline::loadShaders() {
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

void VulkanGraphicsPipeline::createLayout() {
  auto &descriptorMgr = m_device.getDescriptorManager();

  std::unordered_map<u32, std::vector<LX_core::ShaderResourceBinding>>
      setGroups;
  for (const auto &b : m_bindings) {
    setGroups[b.set].push_back(b);
  }

  u32 maxSet = 0;
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
