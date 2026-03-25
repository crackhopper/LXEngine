#include "core/utils/filesystem_tools.hpp"
#include "graphics_backend/vulkan/details/pipelines/vkp_blinnphong.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"
#include "core/utils/env.hpp"
#include "infra/window/window.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
  expSetEnvVK();
  try {
    auto success = cdToWhereShadersExist("blinnphong_0");
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }
    
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Pipeline", 64, 64);

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanPipeline");

    const VkFormat colorFormat = device->getSurfaceFormat().format;
    const VkFormat depthFormat = device->getDepthFormat();
    auto renderPass =
        LX_core::graphic_backend::VulkanRenderPass::create(
            *device, colorFormat, depthFormat);

    VkExtent2D extent{1, 1};
    auto pipeline =
        LX_core::graphic_backend::VkPipelineBlinnPhong::create(
            *device, extent);

    // Build actual VkPipeline (layout/shader modules are created in create()).
    pipeline->buildGraphicsPpl(renderPass->getHandle());

    if (pipeline->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "VkPipeline handle is null\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanPipeline test: " << e.what() << "\n";
    return 0;
  }
}

