#include "vkr_swapchain.hpp"
#include "../vk_device.hpp"
#include "vkr_framebuffer.hpp"
#include "vkr_renderpass.hpp"
#include <algorithm>
#include <stdexcept>

namespace LX_core {
namespace graphic_backend {

VulkanSwapchain::VulkanSwapchain(Token, VulkanDevice &device,
                                 VkSurfaceKHR surface, VkExtent2D extent,
                                 uint32_t graphicsIdx, uint32_t presentIdx,
                                 uint32_t maxFramesInFlight)
    : m_device(&device), m_surface(surface), m_extent(extent),
      m_maxFramesInFlight(maxFramesInFlight) {
}

VulkanSwapchain::~VulkanSwapchain() { cleanup(); }

void VulkanSwapchain::initialize(VulkanRenderPass &renderPass) {
  createInternal(m_extent);
  createImageViews();
  m_depthFormat = renderPass.getDepthFormat();
  createDepthResources();
  createSyncObjects();
  setupFramebuffers(renderPass);
}

void VulkanSwapchain::waitIdle() const {
  if (m_device != nullptr) {
    vkDeviceWaitIdle(m_device->getLogicalDevice());
  }
}

void VulkanSwapchain::cleanup() {
  if (m_device == nullptr) return;
  VkDevice logicalDevice = m_device->getLogicalDevice();

  m_framebuffers.clear();

  for (size_t i = 0; i < m_maxFramesInFlight; i++) {
    if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE)
      vkDestroySemaphore(logicalDevice, m_imageAvailableSemaphores[i], nullptr);
    if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE)
      vkDestroySemaphore(logicalDevice, m_renderFinishedSemaphores[i], nullptr);
    if (m_inFlightFences[i] != VK_NULL_HANDLE)
      vkDestroyFence(logicalDevice, m_inFlightFences[i], nullptr);
  }
  m_imageAvailableSemaphores.clear();
  m_renderFinishedSemaphores.clear();
  m_inFlightFences.clear();

  if (m_depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(logicalDevice, m_depthImageView, nullptr);
    m_depthImageView = VK_NULL_HANDLE;
  }
  if (m_depthImage != VK_NULL_HANDLE) {
    vkDestroyImage(logicalDevice, m_depthImage, nullptr);
    m_depthImage = VK_NULL_HANDLE;
  }
  if (m_depthImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(logicalDevice, m_depthImageMemory, nullptr);
    m_depthImageMemory = VK_NULL_HANDLE;
  }

  for (auto imageView : m_imageViews) {
    vkDestroyImageView(logicalDevice, imageView, nullptr);
  }
  m_imageViews.clear();

  if (m_handle != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(logicalDevice, m_handle, nullptr);
    m_handle = VK_NULL_HANDLE;
  }
}

void VulkanSwapchain::createInternal(VkExtent2D extent) {
  VkPhysicalDevice physDevice = m_device->getPhysicalDevice();

  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, m_surface, &capabilities);

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
  createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t graphicsIdx = m_device->getGraphicsQueueFamilyIndex();
  uint32_t presentIdx = m_device->getPresentQueueFamilyIndex();
  uint32_t queueFamilyIndices[] = {graphicsIdx, presentIdx};

  if (graphicsIdx != presentIdx) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(m_device->getLogicalDevice(), &createInfo, nullptr, &m_handle) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create swap chain!");
  }

  vkGetSwapchainImagesKHR(m_device->getLogicalDevice(), m_handle, &imageCount, nullptr);
  m_images.resize(imageCount);
  vkGetSwapchainImagesKHR(m_device->getLogicalDevice(), m_handle, &imageCount, m_images.data());

  m_imageFormat = createInfo.imageFormat;
  m_extent = extent;
}

void VulkanSwapchain::createImageViews() {
  m_imageViews.resize(m_images.size());
  for (size_t i = 0; i < m_images.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_images[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = m_imageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(m_device->getLogicalDevice(), &createInfo, nullptr, &m_imageViews[i]);
  }
}

void VulkanSwapchain::createDepthResources() {
  if (m_depthFormat == VK_FORMAT_UNDEFINED) {
    throw std::runtime_error("VulkanSwapchain depthFormat not set");
  }
  VkFormat depthFormat = m_depthFormat;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = m_extent.width;
  imageInfo.extent.height = m_extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = depthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_device->getLogicalDevice(), &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create depth image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_device->getLogicalDevice(), m_depthImage, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device->findMemoryTypeIndex(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_device->getLogicalDevice(), &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate depth image memory!");
  }

  vkBindImageMemory(m_device->getLogicalDevice(), m_depthImage, m_depthImageMemory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = depthFormat;
  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  // Only include stencil aspect for depth formats that contain S8.
  if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
      depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device->getLogicalDevice(), &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create depth image view!");
  }
}

void VulkanSwapchain::createSyncObjects() {
  m_imageAvailableSemaphores.resize(m_maxFramesInFlight);
  m_renderFinishedSemaphores.resize(m_maxFramesInFlight);
  m_inFlightFences.resize(m_maxFramesInFlight);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < m_maxFramesInFlight; i++) {
    vkCreateSemaphore(m_device->getLogicalDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
    vkCreateSemaphore(m_device->getLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
    vkCreateFence(m_device->getLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]);
  }
}

void VulkanSwapchain::setupFramebuffers(VulkanRenderPass &renderPass) {
  m_framebuffers.clear();
  std::vector<VkImageView> attachments = {VK_NULL_HANDLE, m_depthImageView};

  for (auto imageView : m_imageViews) {
    attachments[0] = imageView;
    m_framebuffers.push_back(VulkanFrameBuffer::create(
        *m_device, renderPass.getHandle(), attachments, m_extent));
  }
}

void VulkanSwapchain::rebuild(VkExtent2D newExtent, VulkanRenderPass &renderPass) {
  waitIdle();
  cleanup();

  createInternal(newExtent);
  createImageViews();
  m_depthFormat = renderPass.getDepthFormat();
  createDepthResources();
  setupFramebuffers(renderPass);
}

VkResult VulkanSwapchain::acquireNextImage(uint32_t currentFrameIndex, uint32_t &imageIndex) {
  vkWaitForFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[currentFrameIndex], VK_TRUE, UINT64_MAX);

  return vkAcquireNextImageKHR(
      m_device->getLogicalDevice(), m_handle, UINT64_MAX,
      m_imageAvailableSemaphores[currentFrameIndex],
      VK_NULL_HANDLE, &imageIndex);
}

VkResult VulkanSwapchain::present(uint32_t currentFrameIndex, uint32_t imageIndex) {
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[currentFrameIndex];

  VkSwapchainKHR swapChains[] = {m_handle};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  return vkQueuePresentKHR(m_device->getPresentQueue(), &presentInfo);
}

VkSemaphore VulkanSwapchain::getImageAvailableSemaphore(uint32_t i) const {
  return m_imageAvailableSemaphores[i];
}
VkSemaphore VulkanSwapchain::getRenderFinishedSemaphore(uint32_t i) const {
  return m_renderFinishedSemaphores[i];
}
VkFence VulkanSwapchain::getInFlightFence(uint32_t i) const {
  return m_inFlightFences[i];
}

VulkanFrameBuffer &VulkanSwapchain::getFramebuffer(uint32_t index) {
  return *m_framebuffers[index];
}

} // namespace graphic_backend
} // namespace LX_core