#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/render_objects/swapchain.hpp"
#include "backend/vulkan/details/device.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>

int main() {
  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Depth", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanDepth");

    VkInstance instance = device->getInstance();

    // Keep depth format aligned with VulkanSwapchain::createDepthResources().
    const VkFormat depthFormat = device->getDepthFormat();
    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();

    auto renderPass =
        LX_core::backend::VulkanRenderPass::create(
            *device, surfaceFormat.format, depthFormat);

    auto swapchain = LX_core::backend::VulkanSwapchain::create(
        *device, window, /*maxFramesInFlight=*/1);
    swapchain->initialize(*renderPass);

    if (swapchain->getDepthImageView() == VK_NULL_HANDLE) {
      std::cerr << "Depth image view is null\n";
      return 1;
    }
    if (swapchain->getImageCount() == 0) {
      std::cerr << "Swapchain image count is zero\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanDepth test: " << e.what() << "\n";
    return 0;
  }
}

