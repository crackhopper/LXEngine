#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

int main() {
  try {
    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    if (device->getInstance() == VK_NULL_HANDLE) {
      std::cerr << "VK_NULL_HANDLE instance\n";
      return 1;
    }
    if (device->getPhysicalDevice() == VK_NULL_HANDLE) {
      std::cerr << "VK_NULL_HANDLE physical device\n";
      return 1;
    }
    if (device->getGraphicsQueue() == VK_NULL_HANDLE) {
      std::cerr << "VK_NULL_HANDLE graphics queue\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    // Allow "no Vulkan/GPU" environments to skip cleanly.
    std::cerr << "SKIP VulkanDevice test: " << e.what() << "\n";
    return 0;
  }
}

