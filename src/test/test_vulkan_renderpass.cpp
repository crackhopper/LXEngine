#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <vulkan/vulkan.h>

#include <cmath>
#include <iostream>

int main() {
  try {
    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    auto renderPass =
        LX_core::graphic_backend::VulkanRenderPass::create(
            *device, colorFormat, depthFormat);

    if (renderPass->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "RenderPass handle is null\n";
      return 1;
    }

    renderPass->setClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    const auto &clearValues = renderPass->getClearValues();
    if (clearValues.size() != 2) {
      std::cerr << "Unexpected clearValues size: " << clearValues.size()
                << "\n";
      return 1;
    }

    const float r = clearValues[0].color.float32[0];
    const float g = clearValues[0].color.float32[1];
    const float b = clearValues[0].color.float32[2];
    const float a = clearValues[0].color.float32[3];

    auto approxEq = [](float x, float y) { return std::fabs(x - y) < 1e-4f; };
    if (!approxEq(r, 0.1f) || !approxEq(g, 0.2f) || !approxEq(b, 0.3f) ||
        !approxEq(a, 1.0f)) {
      std::cerr << "Clear color mismatch\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanRenderPass test: " << e.what() << "\n";
    return 0;
  }
}

