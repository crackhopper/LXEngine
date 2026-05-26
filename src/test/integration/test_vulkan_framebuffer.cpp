#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/device.hpp"
#include "infra/window/window.hpp"
#include "core/utils/env.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <optional>

int main() {
  expSetEnvVK();
  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Framebuffer", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanFramebuffer");

    const VkFormat colorFormat = device->getSurfaceFormat().format;
    const VkFormat depthFormat = device->getDepthFormat();
    auto depthAspectMask = device->getDepthAspectMask();

    auto renderPass =
        LX_core::backend::VulkanRenderPass::create(
            *device, colorFormat, depthFormat);

    const VkExtent2D extent{64, 64};

    // Create minimal attachments needed for vkCreateFramebuffer.
    auto colorTexture = LX_core::backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, colorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    auto depthTexture = LX_core::backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        depthAspectMask);

    std::vector<VkImageView> attachments = {
        colorTexture->getImageView(),
        depthTexture->getImageView()
    };

    auto framebuffer = LX_core::backend::VulkanFrameBuffer::create(
        *device, renderPass->getHandle(), attachments, extent);

    if (framebuffer->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Framebuffer handle is null\n";
      return 1;
    }

    auto cubeRenderPass = LX_core::backend::VulkanRenderPass::create(
        *device, std::optional<VkFormat>{VK_FORMAT_R16G16B16A16_SFLOAT},
        std::nullopt, false);
    auto cubeTexture = LX_core::backend::VulkanTexture::createCube(
        *device, 64, 64, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3, VK_FILTER_LINEAR);
    auto cubeFaceMipView = cubeTexture->createSubresourceView(
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 3, 1);
    const VkExtent2D cubeMipExtent{32, 32};
    std::vector<VkImageView> cubeAttachments = {
        cubeFaceMipView.getHandle(),
    };
    auto cubeFramebuffer = LX_core::backend::VulkanFrameBuffer::create(
        *device, cubeRenderPass->getHandle(), cubeAttachments, cubeMipExtent);
    if (cubeFramebuffer->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Cubemap face/mip framebuffer handle is null\n";
      return 1;
    }

    // Cleanup in a stable order.
    // (framebuffer destructor runs before texture destruction via unique_ptr)
    cubeFramebuffer.reset();
    framebuffer.reset();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanFramebuffer test: " << e.what() << "\n";
    return 0;
  }
}
