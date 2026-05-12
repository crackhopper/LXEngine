#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/render_objects/swapchain_extent.hpp"
#include "backend/vulkan/details/render_objects/swapchain.hpp"
#include "backend/vulkan/details/device.hpp"
#include "core/utils/env.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace {

bool testResolveSwapchainExtentRejectsZeroCurrentExtent() {
  VkSurfaceCapabilitiesKHR capabilities{};
  capabilities.currentExtent = {0, 0};

  const std::optional<VkExtent2D> extent =
      LX_core::backend::resolveSwapchainExtent(
          VkExtent2D{1280, 720}, capabilities);
  if (extent.has_value()) {
    std::cerr << "resolveSwapchainExtent should defer rebuild when current "
                 "surface extent is zero\n";
    return false;
  }
  return true;
}

bool testResolveSwapchainExtentRejectsClampedZeroExtent() {
  VkSurfaceCapabilitiesKHR capabilities{};
  capabilities.currentExtent = {std::numeric_limits<uint32_t>::max(),
                                std::numeric_limits<uint32_t>::max()};
  capabilities.minImageExtent = {0, 0};
  capabilities.maxImageExtent = {0, 0};

  const std::optional<VkExtent2D> extent =
      LX_core::backend::resolveSwapchainExtent(
          VkExtent2D{1280, 720}, capabilities);
  if (extent.has_value()) {
    std::cerr << "resolveSwapchainExtent should reject clamped zero extent\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  expSetEnvVK();
  try {
    if (!testResolveSwapchainExtentRejectsZeroCurrentExtent()) {
      return 1;
    }
    if (!testResolveSwapchainExtentRejectsClampedZeroExtent()) {
      return 1;
    }

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

    if (swapchain->getDepthImageView(0) == VK_NULL_HANDLE) {
      std::cerr << "Depth image view is null\n";
      return 1;
    }
    if (swapchain->getImageCount() == 0) {
      std::cerr << "Swapchain image count is zero\n";
      return 1;
    }
    if (swapchain->getRenderFinishedSemaphoreCount() !=
        swapchain->getImageCount()) {
      std::cerr << "Render-finished semaphores must be allocated per "
                   "swapchain image to avoid present reuse hazards\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanDepth test: " << e.what() << "\n";
    return 0;
  }
}
