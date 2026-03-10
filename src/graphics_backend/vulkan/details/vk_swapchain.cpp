#include "vk_swapchain.hpp"
#include <stdexcept>

namespace LX_core::graphic_backend {

VulkanSwapchain::~VulkanSwapchain() { shutdown(); }

void VulkanSwapchain::initialize(VulkanDevice *device, VkSurfaceKHR surf,
                                 uint32_t w, uint32_t h) {
  p_device = device;
  surface = surf;
  width = w;
  height = h;

  createSwapchain();
  createImageViews();
}

void VulkanSwapchain::shutdown() {

  VkDevice device = p_device->getDevice();

  for (auto view : imageViews) {
    vkDestroyImageView(device, view, nullptr);
  }

  imageViews.clear();

  if (swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
  }
}

void VulkanSwapchain::createSwapchain() {

  VkDevice device = p_device->getDevice();

  format = VK_FORMAT_B8G8R8A8_SRGB;
  extent = {width, height};

  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface;
  info.minImageCount = 2;
  info.imageFormat = format;
  info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  info.imageExtent = extent;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t queueFamily = p_device->getGraphicsQueueFamilyIndex();

  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.queueFamilyIndexCount = 1;
  info.pQueueFamilyIndices = &queueFamily;

  info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;

  if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create swapchain");
  }

  uint32_t imageCount = 0;

  vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);

  images.resize(imageCount);

  vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());
}

void VulkanSwapchain::createImageViews() {

  VkDevice device = p_device->getDevice();

  imageViews.resize(images.size());

  for (size_t i = 0; i < images.size(); ++i) {

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = images[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;

    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &view, nullptr, &imageViews[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create image view");
    }
  }
}

} // namespace LX_core::graphic_backend