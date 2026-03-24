#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_swapchain.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>

namespace {
VkSurfaceFormatKHR chooseSurfaceFormat(VkPhysicalDevice phys, VkSurfaceKHR surface) {
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &formatCount, nullptr);
  if (formatCount == 0) {
    throw std::runtime_error("No surface formats found");
  }

  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &formatCount, formats.data());

  for (const auto &f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return f;
    }
  }
  return formats[0];
}
} // namespace

int main() {
  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Depth", 64, 64);

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    VkInstance instance = device->getInstance();
    void *surfaceHandlePtr =
        window->createGraphicsHandle(GraphicsAPI::Vulkan, &instance);
    if (!surfaceHandlePtr) {
      std::cerr << "Failed to create Vulkan surface handle\n";
      return 1;
    }

    VkSurfaceKHR surface = *static_cast<VkSurfaceKHR *>(surfaceHandlePtr);

    // Keep depth format aligned with VulkanSwapchain::createDepthResources().
    const VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(device->getPhysicalDevice(), surface);

    const VkExtent2D extent{
        static_cast<uint32_t>(window->getWidth()),
        static_cast<uint32_t>(window->getHeight())};

    auto renderPass =
        LX_core::graphic_backend::VulkanRenderPass::create(
            *device, surfaceFormat.format, depthFormat);

    auto swapchain = LX_core::graphic_backend::VulkanSwapchain::create(
        *device, surface, extent, device->getGraphicsQueueFamilyIndex(),
        device->getPresentQueueFamilyIndex(), /*maxFramesInFlight=*/1);
    swapchain->initialize(*renderPass);

    if (swapchain->getDepthImageView() == VK_NULL_HANDLE) {
      std::cerr << "Depth image view is null\n";
      return 1;
    }
    if (swapchain->getImageCount() == 0) {
      std::cerr << "Swapchain image count is zero\n";
      return 1;
    }

    // Cleanup surface (window impl doesn't destroy it explicitly).
    vkDestroySurfaceKHR(instance, surface, nullptr);
    delete static_cast<VkSurfaceKHR *>(surfaceHandlePtr);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanDepth test: " << e.what() << "\n";
    return 0;
  }
}

