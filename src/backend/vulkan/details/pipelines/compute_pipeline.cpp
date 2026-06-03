#include "compute_pipeline.hpp"

#include "backend/vulkan/details/descriptors/descriptor_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace LX_core::backend {
namespace {

[[nodiscard]] VkShaderModule createShaderModule(VkDevice device,
                                                const std::vector<u32> &code) {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size() * sizeof(u32);
  info.pCode = code.data();
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
    throw std::runtime_error("failed to create compute shader module");
  }
  return module;
}

} // namespace

VulkanComputePipeline::VulkanComputePipeline(Token, VulkanDevice &device,
                                             const PipelineBuildDesc &buildInfo)
    : m_device(device), m_deviceHandle(device.getLogicalDevice()),
      m_bindings(buildInfo.bindings), m_pushConstant(buildInfo.pushConstant) {
  if (buildInfo.type != PipelineBuildType::Compute) {
    throw std::runtime_error("compute pipeline requires compute build desc");
  }
  const auto stageIt =
      std::find_if(buildInfo.stages.begin(), buildInfo.stages.end(),
                   [](const ShaderStageCode &stage) {
                     return stage.stage == ShaderStage::Compute;
                   });
  if (stageIt == buildInfo.stages.end()) {
    throw std::runtime_error("compute pipeline missing compute shader stage");
  }

  std::unordered_map<u32, std::vector<ShaderResourceBinding>> setGroups;
  for (const ShaderResourceBinding &binding : m_bindings) {
    setGroups[binding.set].push_back(binding);
  }

  u32 maxSetIndex = 0;
  for (const auto &[setIndex, setBindings] : setGroups) {
    (void)setBindings;
    maxSetIndex = std::max(maxSetIndex, setIndex);
  }

  std::vector<VkDescriptorSetLayout> setLayouts;
  setLayouts.resize(setGroups.empty() ? 0 : maxSetIndex + 1, VK_NULL_HANDLE);
  for (const auto &[setIndex, setBindings] : setGroups) {
    setLayouts[setIndex] =
        m_device.getDescriptorManager().getOrCreateLayout(setBindings);
  }

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
  layoutInfo.pSetLayouts = setLayouts.data();
  if (vkCreatePipelineLayout(m_deviceHandle, &layoutInfo, nullptr, &m_layout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create compute pipeline layout");
  }

  m_shaderModule = createShaderModule(m_deviceHandle, stageIt->bytecode);

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = m_shaderModule;
  pipelineInfo.stage.pName = "main";
  pipelineInfo.layout = m_layout;
  if (vkCreateComputePipelines(m_deviceHandle, VK_NULL_HANDLE, 1, &pipelineInfo,
                               nullptr, &m_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create compute pipeline");
  }
}

VulkanComputePipeline::~VulkanComputePipeline() {
  if (m_deviceHandle == VK_NULL_HANDLE) {
    return;
  }
  if (m_pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(m_deviceHandle, m_pipeline, nullptr);
  }
  if (m_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(m_deviceHandle, m_layout, nullptr);
  }
  if (m_shaderModule != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_deviceHandle, m_shaderModule, nullptr);
  }
}

} // namespace LX_core::backend
