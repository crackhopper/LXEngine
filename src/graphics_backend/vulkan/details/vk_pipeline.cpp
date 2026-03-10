#include "vk_pipeline.hpp"

#include <fstream>

namespace LX_core::graphic_backend {

static std::vector<char> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  size_t size = (size_t)file.tellg();
  std::vector<char> buffer(size);

  file.seekg(0);
  file.read(buffer.data(), size);

  return buffer;
}

VulkanShaderModule::VulkanShaderModule(VulkanDeviceWeakPtr device)
    : m_device(device) {}

VulkanShaderModule::~VulkanShaderModule() {

  auto dev = m_device.lock();
  if (!dev)
    return;

  if (m_module)
    vkDestroyShaderModule(dev->getHandle(), m_module, nullptr);
}

bool VulkanShaderModule::loadFromFile(const std::string &path) {

  auto dev = m_device.lock();
  if (!dev)
    return false;

  auto code = readFile(path);

  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t *>(code.data());

  return vkCreateShaderModule(dev->getHandle(), &info, nullptr, &m_module) ==
         VK_SUCCESS;
}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDeviceWeakPtr device)
    : m_device(device) {}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {

  auto dev = m_device.lock();
  if (!dev)
    return;

  if (m_layout)
    vkDestroyDescriptorSetLayout(dev->getHandle(), m_layout, nullptr);
}

void VulkanDescriptorSetLayout::addBinding(uint32_t binding,
                                           VkDescriptorType type,
                                           VkShaderStageFlags stageFlags) {

  VkDescriptorSetLayoutBinding b{};
  b.binding = binding;
  b.descriptorType = type;
  b.descriptorCount = 1;
  b.stageFlags = stageFlags;

  m_bindings.push_back(b);
}

bool VulkanDescriptorSetLayout::build() {

  auto dev = m_device.lock();
  if (!dev)
    return false;

  VkDescriptorSetLayoutCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.bindingCount = static_cast<uint32_t>(m_bindings.size());
  info.pBindings = m_bindings.data();

  return vkCreateDescriptorSetLayout(dev->getHandle(), &info, nullptr,
                                     &m_layout) == VK_SUCCESS;
}

VulkanPipelineLayout::VulkanPipelineLayout(VulkanDeviceWeakPtr device)
    : m_device(device) {}

VulkanPipelineLayout::~VulkanPipelineLayout() {

  auto dev = m_device.lock();
  if (!dev)
    return;

  if (m_layout)
    vkDestroyPipelineLayout(dev->getHandle(), m_layout, nullptr);
}

void VulkanPipelineLayout::addDescriptorSetLayout(
    VkDescriptorSetLayout layout) {
  m_descriptorLayouts.push_back(layout);
}

bool VulkanPipelineLayout::build() {

  auto dev = m_device.lock();
  if (!dev)
    return false;

  VkPipelineLayoutCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.setLayoutCount = static_cast<uint32_t>(m_descriptorLayouts.size());
  info.pSetLayouts = m_descriptorLayouts.data();

  return vkCreatePipelineLayout(dev->getHandle(), &info, nullptr, &m_layout) ==
         VK_SUCCESS;
}

VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanDeviceWeakPtr device)
    : m_device(device) {}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {

  auto dev = m_device.lock();
  if (!dev)
    return;

  if (m_pipeline)
    vkDestroyPipeline(dev->getHandle(), m_pipeline, nullptr);
}

bool VulkanGraphicsPipeline::build(VkRenderPass renderPass,
                                   VkPipelineLayout layout, VkExtent2D extent,
                                   VulkanShaderModule *vert,
                                   VulkanShaderModule *frag) {

  auto dev = m_device.lock();
  if (!dev)
    return false;

  VkPipelineShaderStageCreateInfo stages[2]{};

  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert->module();
  stages[0].pName = "main";

  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag->module();
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport viewport{};
  viewport.width = (float)extent.width;
  viewport.height = (float)extent.height;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.extent = extent;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.lineWidth = 1.0f;
  raster.cullMode = VK_CULL_MODE_BACK_BIT;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo blendState{};
  blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blendState.attachmentCount = 1;
  blendState.pAttachments = &blend;

  VkGraphicsPipelineCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewportState;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &ms;
  info.pColorBlendState = &blendState;
  info.layout = layout;
  info.renderPass = renderPass;

  return vkCreateGraphicsPipelines(dev->getHandle(), VK_NULL_HANDLE, 1, &info,
                                   nullptr, &m_pipeline) == VK_SUCCESS;
}

} // namespace LX_core::graphic_backend