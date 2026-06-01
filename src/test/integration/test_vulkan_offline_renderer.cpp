#include "backend/vulkan/vulkan_offline_renderer.hpp"
#include "core/utils/env.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  expSetEnvVK();
  try {
    auto renderer = LX_core::backend::VulkanOfflineRenderer::create(
        "TestVulkanOfflineRenderer");
    if (!renderer || !renderer->isInitialized()) {
      std::cerr << "offline renderer did not initialize\n";
      return 1;
    }
    if (!renderer->isHeadless()) {
      std::cerr << "offline renderer foundation is not headless\n";
      return 1;
    }

    auto &foundation = renderer->foundation();
    if (foundation.device().getLogicalDevice() == VK_NULL_HANDLE) {
      std::cerr << "headless logical device is null\n";
      return 1;
    }
    if (foundation.device().getGraphicsQueue() == VK_NULL_HANDLE) {
      std::cerr << "headless graphics queue is null\n";
      return 1;
    }
    if (foundation.device().getSurface() != VK_NULL_HANDLE) {
      std::cerr << "headless foundation unexpectedly created a surface\n";
      return 1;
    }

    auto cmd = foundation.commandBufferManager().beginSingleTimeCommands();
    foundation.commandBufferManager().endSingleTimeCommands(
        std::move(cmd), foundation.device().getGraphicsQueue());

    std::cout << "test_vulkan_offline_renderer passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "test_vulkan_offline_renderer failed: " << error.what()
              << "\n";
    return 1;
  }
}
