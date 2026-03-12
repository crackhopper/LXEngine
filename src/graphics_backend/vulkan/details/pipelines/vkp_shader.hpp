#pragma once
#include "../vk_device.hpp"
#include <string>

namespace LX_core::graphic_backend {
  class VulkanShaderModule {
public:
  explicit VulkanShaderModule(VulkanDevice &device);
  ~VulkanShaderModule();

  bool loadFromFile(const std::string &path);

  VkShaderModule module() const { return hModule; }

private:
  VkDevice hDevice = VK_NULL_HANDLE;
  VkShaderModule hModule = VK_NULL_HANDLE;
};
}
