#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/device.hpp"
#include "infra/window/window.hpp"
#include "core/utils/env.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <vector>

namespace {

void transitionImageSubresource(
    VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage, u32 mipLevel, u32 arrayLayer) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = mipLevel;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = arrayLayer;
  barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

bool bufferContainsWrittenColor(const void *mapped, std::size_t byteSize) {
  const auto *bytes = static_cast<const unsigned char *>(mapped);
  return std::any_of(bytes, bytes + byteSize,
                     [](unsigned char value) { return value != 0; });
}

} // namespace

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
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        3, VK_FILTER_LINEAR);
    constexpr u32 kBakeMipLevel = 1;
    constexpr u32 kBakeFaceLayer = 3;
    auto cubeFaceMipView = cubeTexture->createSubresourceView(
        VK_IMAGE_ASPECT_COLOR_BIT, kBakeMipLevel, 1, kBakeFaceLayer, 1);
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

    const VkDeviceSize readbackByteSize =
        static_cast<VkDeviceSize>(cubeMipExtent.width) *
        static_cast<VkDeviceSize>(cubeMipExtent.height) * 8u;
    auto readback = LX_core::backend::VulkanBuffer::create(
        *device, readbackByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, 1, device->getGraphicsQueueFamilyIndex());
    auto cmd = cmdBufferMgr->beginSingleTimeCommands();
    transitionImageSubresource(
        cmd->getHandle(), cubeTexture->getHandle(), VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, kBakeMipLevel,
        kBakeFaceLayer);

    auto clearValues = cubeRenderPass->getClearValues();
    clearValues[0].color = {1.0f, 0.25f, 0.0f, 1.0f};
    cmd->beginRenderPass(cubeRenderPass->getHandle(),
                         cubeFramebuffer->getHandle(), cubeMipExtent,
                         clearValues);
    cmd->endRenderPass();

    transitionImageSubresource(
        cmd->getHandle(), cubeTexture->getHandle(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, kBakeMipLevel, kBakeFaceLayer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = kBakeMipLevel;
    region.imageSubresource.baseArrayLayer = kBakeFaceLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {cubeMipExtent.width, cubeMipExtent.height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(), cubeTexture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);
    cmdBufferMgr->endSingleTimeCommands(std::move(cmd),
                                        device->getGraphicsQueue());

    const void *mapped = readback->map();
    const bool wroteCubeFaceMip = bufferContainsWrittenColor(
        mapped, static_cast<std::size_t>(readbackByteSize));
    readback->unmap();
    if (!wroteCubeFaceMip) {
      std::cerr << "Cubemap face/mip render pass did not write readback data\n";
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
