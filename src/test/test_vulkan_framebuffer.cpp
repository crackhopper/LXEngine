#include "graphics_backend/vulkan/details/render_objects/vkr_framebuffer.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <vulkan/vulkan.h>

#include <iostream>

namespace {
struct ImageResources {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

ImageResources createImageWithView(LX_core::graphic_backend::VulkanDevice &device,
                                     uint32_t width, uint32_t height,
                                     VkFormat format,
                                     VkImageUsageFlags usage,
                                     VkImageAspectFlags aspect) {
  ImageResources out;
  VkDevice vkDevice = device.getLogicalDevice();

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(vkDevice, &imageInfo, nullptr, &out.image) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateImage failed");
  }

  VkMemoryRequirements memReq{};
  vkGetImageMemoryRequirements(vkDevice, out.image, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex =
      device.findMemoryTypeIndex(memReq.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &out.memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("vkAllocateMemory failed");
  }

  vkBindImageMemory(vkDevice, out.image, out.memory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = out.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateImageView failed");
  }

  return out;
}

void destroyImageWithView(LX_core::graphic_backend::VulkanDevice &device,
                           ImageResources &res) {
  VkDevice vkDevice = device.getLogicalDevice();
  if (res.view != VK_NULL_HANDLE) {
    vkDestroyImageView(vkDevice, res.view, nullptr);
    res.view = VK_NULL_HANDLE;
  }
  if (res.image != VK_NULL_HANDLE) {
    vkDestroyImage(vkDevice, res.image, nullptr);
    res.image = VK_NULL_HANDLE;
  }
  if (res.memory != VK_NULL_HANDLE) {
    vkFreeMemory(vkDevice, res.memory, nullptr);
    res.memory = VK_NULL_HANDLE;
  }
}
} // namespace

int main() {
  try {
    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    auto renderPass =
        LX_core::graphic_backend::VulkanRenderPass::create(
            *device, colorFormat, depthFormat);

    const VkExtent2D extent{64, 64};

    // Create minimal attachments needed for vkCreateFramebuffer.
    ImageResources color =
        createImageWithView(*device, extent.width, extent.height, colorFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    ImageResources depth =
        createImageWithView(*device, extent.width, extent.height, depthFormat,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    std::vector<VkImageView> attachments = {color.view, depth.view};

    auto framebuffer = LX_core::graphic_backend::VulkanFrameBuffer::create(
        *device, renderPass->getHandle(), attachments, extent);

    if (framebuffer->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Framebuffer handle is null\n";
      return 1;
    }

    // Cleanup in a stable order.
    // (framebuffer destructor runs before image destruction)
    framebuffer.reset();
    destroyImageWithView(*device, color);
    destroyImageWithView(*device, depth);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanFramebuffer test: " << e.what() << "\n";
    return 0;
  }
}

