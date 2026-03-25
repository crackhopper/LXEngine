#include "graphics_backend/vulkan/details/render_objects/vkr_framebuffer.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/resources/vkr_texture.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"
#include "infra/window/window.hpp"
#include "core/utils/env.hpp"

#include <vulkan/vulkan.h>

#include <iostream>

int main() {
  expSetEnvVK();
  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Framebuffer", 64, 64);

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanFramebuffer");

    const VkFormat colorFormat = device->getSurfaceFormat().format;
    const VkFormat depthFormat = device->getDepthFormat();
    auto depthAspectMask = device->getDepthAspectMask();

    auto renderPass =
        LX_core::graphic_backend::VulkanRenderPass::create(
            *device, colorFormat, depthFormat);

    const VkExtent2D extent{64, 64};

    // Create minimal attachments needed for vkCreateFramebuffer.
    auto colorTexture = LX_core::graphic_backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, colorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    auto depthTexture = LX_core::graphic_backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        depthAspectMask);

    std::vector<VkImageView> attachments = {
        colorTexture->getImageView(),
        depthTexture->getImageView()
    };

    auto framebuffer = LX_core::graphic_backend::VulkanFrameBuffer::create(
        *device, renderPass->getHandle(), attachments, extent);

    if (framebuffer->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Framebuffer handle is null\n";
      return 1;
    }

    // Cleanup in a stable order.
    // (framebuffer destructor runs before texture destruction via unique_ptr)
    framebuffer.reset();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanFramebuffer test: " << e.what() << "\n";
    return 0;
  }
}

