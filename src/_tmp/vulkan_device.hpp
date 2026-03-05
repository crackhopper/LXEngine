#pragma once
#include "../device.hpp"
#include <vulkan/vulkan.h>

namespace LX_core {
class VulkanDevice : public Device {
public:
  VulkanDevice();
  ~VulkanDevice();

private:
  static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

  const std::vector<const char *> validationLayers = {
      "VK_LAYER_KHRONOS_validation"};

  const std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};


  VkCommandPool commandPool;
};
} // namespace LX_graphics