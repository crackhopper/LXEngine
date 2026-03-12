#include "vkp_shader.hpp"
#include "infra/fs/file.hpp"

namespace LX_core::graphic_backend {

VulkanShaderModule::VulkanShaderModule(VulkanDevice &device)
    : hDevice(device.getHandle()) {}

VulkanShaderModule::~VulkanShaderModule() {
  if (hModule)
    vkDestroyShaderModule(hDevice, hModule, nullptr);
    hModule = VK_NULL_HANDLE; 
}

bool VulkanShaderModule::loadFromFile(const std::string &path) {
  auto code = readFile(path);

  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t *>(code.data());

  return vkCreateShaderModule(hDevice, &info, nullptr,
                              &hModule) == VK_SUCCESS;
}

} // namespace LX_core::graphic_backend