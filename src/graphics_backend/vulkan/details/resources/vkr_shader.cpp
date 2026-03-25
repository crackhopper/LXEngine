#include "vkr_shader.hpp"
#include "../vk_device.hpp"
#include "core/utils/filesystem_tools.hpp"
#include <fstream>
#include <stdexcept>

namespace LX_core {
namespace graphic_backend {

VulkanShader::VulkanShader(Token, VulkanDevice &device, const std::string &name,
                         VkShaderStageFlagBits stage)
    : m_device(device.getLogicalDevice()), m_stage(stage) {
  // Determine shader file path based on stage
  auto shaderPath = getShaderPath(name);
  if (shaderPath.empty()) {
    throw std::runtime_error("Failed to find shader file: " + name);
  }

  // Load shader bytecode from file
  std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + shaderPath);
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  // Create shader module
  VkShaderModuleCreateInfo moduleInfo{};
  moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  moduleInfo.codeSize = buffer.size();
  moduleInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

  if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &m_module) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shader module: " + name);
  }
}

VulkanShader::~VulkanShader() {
  if (m_device != VK_NULL_HANDLE && m_module != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, m_module, nullptr);
    m_module = VK_NULL_HANDLE;
  }
}

VkPipelineShaderStageCreateInfo VulkanShader::getStageCreateInfo() const {
  VkPipelineShaderStageCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  createInfo.stage = m_stage;
  createInfo.module = m_module;
  createInfo.pName = m_entryPoint.c_str();
  createInfo.flags = 0;
  createInfo.pNext = nullptr;
  return createInfo;
}

} // namespace graphic_backend
} // namespace LX_core
