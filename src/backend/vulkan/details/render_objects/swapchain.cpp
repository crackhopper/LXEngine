#include "swapchain.hpp"
#include "../device.hpp"
#include "framebuffer.hpp"
#include "render_pass.hpp"
#include <algorithm>
#include <limits>
#include <utility>
#include <stdexcept>

namespace LX_core {
namespace backend {

VulkanSwapchain::VulkanSwapchain(Token, VulkanDevice &device,
                                 WindowSharedPtr window,
                                 u32 maxFramesInFlight)
    : m_device(device), m_window(std::move(window)),
      m_maxFramesInFlight(maxFramesInFlight) {
  if (!m_window) {
    throw std::runtime_error("VulkanSwapchain: window is null");
  }

  m_surface = m_device.getSurface();
  // Raw extent from the current window size; createInternal() will
  // clamp it using VkSurfaceCapabilitiesKHR.
  m_extent = {static_cast<u32>(m_window->getWidth()),
              static_cast<u32>(m_window->getHeight())};
}

VulkanSwapchain::~VulkanSwapchain() { cleanup(); }

void VulkanSwapchain::initialize(VulkanRenderPass &renderPass) {
  m_surfaceFormat = m_device.getSurfaceFormat();
  m_depthFormat = m_device.getDepthFormat();
  m_depthAspectMask = m_device.getDepthAspectMask();
  createInternal(m_extent);
  createImageViews();
  createDepthResources();
  createSyncObjects();
  setupFramebuffers(renderPass);
}

void VulkanSwapchain::waitIdle() const {
  vkDeviceWaitIdle(m_device.getLogicalDevice());
}

void VulkanSwapchain::cleanup() {
  VkDevice logicalDevice = m_device.getLogicalDevice();

  m_framebuffers.clear();

  for (VkSemaphore semaphore : m_imageAvailableSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(logicalDevice, semaphore, nullptr);
    }
  }
  for (VkSemaphore semaphore : m_renderFinishedSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(logicalDevice, semaphore, nullptr);
    }
  }
  for (VkFence fence : m_inFlightFences) {
    if (fence != VK_NULL_HANDLE) {
      vkDestroyFence(logicalDevice, fence, nullptr);
    }
  }
  m_imageAvailableSemaphores.clear();
  m_renderFinishedSemaphores.clear();
  m_inFlightFences.clear();
  m_imagesInFlight.clear();

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

void VulkanSwapchain::createInternal(VkExtent2D extent,
                                     VkSwapchainKHR oldSwapchain) {
  // Vulkan spec: currentExtent == UINT32_MAX means it's not fixed by the
  // surface; otherwise it is the exact value we must use.
  VkPhysicalDevice physDevice = m_device.getPhysicalDevice();
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, m_surface,
                                            &capabilities);

  if (extent.width == 0 || extent.height == 0) {
    throw std::runtime_error(
        "VulkanSwapchain: window extent is zero (minimized?)");
  }

  VkExtent2D actualExtent{};
  if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
    actualExtent = capabilities.currentExtent;
  } else {
    actualExtent = extent;
    actualExtent.width = std::clamp(
        actualExtent.width, capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(
        actualExtent.height, capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);
  }

  if (actualExtent.width == 0 || actualExtent.height == 0) {
    throw std::runtime_error("VulkanSwapchain: clamped extent is zero");
  }

  m_extent = actualExtent;

  u32 imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = m_device.getSurfaceFormat().format;
  createInfo.imageColorSpace = m_device.getSurfaceFormat().colorSpace;
  createInfo.imageExtent = m_extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  u32 graphicsIdx = m_device.getGraphicsQueueFamilyIndex();
  u32 presentIdx = m_device.getPresentQueueFamilyIndex();
  u32 queueFamilyIndices[] = {graphicsIdx, presentIdx};

  if (graphicsIdx != presentIdx) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode =
      VK_PRESENT_MODE_FIFO_KHR; // TODO : 应该有 chooseSwapPresentMode 方法。

  // 重构前旧代码标注：（不要删除）
  //     LX_graphics::SwapChainSupportDetails swapChainSupport =
  //         querySwapChainSupport(physicalDevice);
  //     VkSurfaceFormatKHR surfaceFormat =
  //         chooseSwapSurfaceFormat(swapChainSupport.formats);
  //     VkPresentModeKHR presentMode =
  //         chooseSwapPresentMode(swapChainSupport.presentModes);

  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = oldSwapchain;

  if (vkCreateSwapchainKHR(m_device.getLogicalDevice(), &createInfo, nullptr,
                           &m_handle) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create swap chain!");
  }

  vkGetSwapchainImagesKHR(m_device.getLogicalDevice(), m_handle, &imageCount,
                          nullptr);
  m_images.resize(imageCount);
  vkGetSwapchainImagesKHR(m_device.getLogicalDevice(), m_handle, &imageCount,
                          m_images.data());
}

void VulkanSwapchain::createImageViews() {
  m_imageViews.resize(m_images.size());
  for (usize i = 0; i < m_images.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_images[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = m_device.getSurfaceFormat().format;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(m_device.getLogicalDevice(), &createInfo, nullptr,
                      &m_imageViews[i]);
  }
}

void VulkanSwapchain::createDepthResources() {
  if (m_depthFormat == VK_FORMAT_UNDEFINED) {
    throw std::runtime_error("VulkanSwapchain depthFormat not set");
  }
  const VkFormat depthFormat = m_depthFormat;
  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  // Only include stencil aspect for depth formats that contain S8.
  if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
      depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  VkDevice logicalDevice = m_device.getLogicalDevice();

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

  if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &m_depthImage) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create depth image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(logicalDevice, m_depthImage, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device.findMemoryTypeIndex(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr,
                       &m_depthImageMemory) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate depth image memory!");
  }

  vkBindImageMemory(logicalDevice, m_depthImage, m_depthImageMemory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = depthFormat;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(logicalDevice, &viewInfo, nullptr,
                        &m_depthImageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create depth image view!");
  }

  transitionDepthImageToOptimal();
}

void VulkanSwapchain::transitionDepthImageToOptimal() {
  if (m_depthImage == VK_NULL_HANDLE) {
    return;
  }

  VkDevice logicalDevice = m_device.getLogicalDevice();

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = m_device.getGraphicsQueueFamilyIndex();

  VkCommandPool transientPool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &transientPool) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "VulkanSwapchain: failed to create transient command pool for depth "
        "layout init");
  }

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = transientPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &cmd) != VK_SUCCESS) {
    vkDestroyCommandPool(logicalDevice, transientPool, nullptr);
    throw std::runtime_error(
        "VulkanSwapchain: failed to allocate transient command buffer for "
        "depth layout init");
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  if (m_depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
      m_depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_depthImage;
  barrier.subresourceRange.aspectMask = aspectMask;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  VkQueue queue = m_device.getGraphicsQueue();
  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkFreeCommandBuffers(logicalDevice, transientPool, 1, &cmd);
  vkDestroyCommandPool(logicalDevice, transientPool, nullptr);
}

void VulkanSwapchain::createSyncObjects() {
  m_imageAvailableSemaphores.resize(m_maxFramesInFlight);
  m_renderFinishedSemaphores.resize(m_maxFramesInFlight);
  m_inFlightFences.resize(m_maxFramesInFlight);
  m_imagesInFlight.assign(m_images.size(), VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (usize i = 0; i < m_maxFramesInFlight; i++) {
    vkCreateSemaphore(m_device.getLogicalDevice(), &semaphoreInfo, nullptr,
                      &m_imageAvailableSemaphores[i]);
    vkCreateSemaphore(m_device.getLogicalDevice(), &semaphoreInfo, nullptr,
                      &m_renderFinishedSemaphores[i]);
    vkCreateFence(m_device.getLogicalDevice(), &fenceInfo, nullptr,
                  &m_inFlightFences[i]);
  }
}

void VulkanSwapchain::setupFramebuffers(VulkanRenderPass &renderPass) {
  m_framebuffers.clear();
  std::vector<VkImageView> attachments = {VK_NULL_HANDLE, m_depthImageView};

  for (auto imageView : m_imageViews) {
    attachments[0] = imageView;
    m_framebuffers.push_back(VulkanFrameBuffer::create(
        m_device, renderPass.getHandle(), attachments, m_extent));
  }
}

void VulkanSwapchain::rebuild(VulkanRenderPass &renderPass) {
  waitIdle();

  // Keep the old swapchain alive across cleanup so we can hand it to the
  // driver as VkSwapchainCreateInfoKHR::oldSwapchain — the driver is
  // allowed to reuse the underlying allocation chain that way, which
  // observably stabilizes rebuild on Optimus / cross-GPU PRIME paths.
  VkSwapchainKHR oldHandle = m_handle;
  m_handle = VK_NULL_HANDLE; // hide it from cleanup() so it isn't destroyed yet

  cleanup();

  VkExtent2D rawExtent{
      static_cast<u32>(m_window->getWidth()),
      static_cast<u32>(m_window->getHeight())};
  createInternal(rawExtent, oldHandle);
  createImageViews();
  m_depthFormat = m_device.getDepthFormat();
  createDepthResources();
  createSyncObjects(); // 重新创建 semaphores 和 fences
  setupFramebuffers(renderPass);

  // Spec requires oldSwapchain to be destroyed by the application after the
  // new swapchain is in use. We've already finished creating the new one and
  // its image views / framebuffers, so it's safe to retire the old handle now.
  if (oldHandle != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(m_device.getLogicalDevice(), oldHandle, nullptr);
  }
}

VkResult VulkanSwapchain::acquireNextImage(u32 currentFrameIndex,
                                           u32 &imageIndex) {
  waitForFrame(currentFrameIndex);

  VkResult result =
      vkAcquireNextImageKHR(m_device.getLogicalDevice(), m_handle, UINT64_MAX,
                            m_imageAvailableSemaphores[currentFrameIndex],
                            VK_NULL_HANDLE, &imageIndex);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return result;
  }

  if (imageIndex < m_imagesInFlight.size() &&
      m_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
    vkWaitForFences(m_device.getLogicalDevice(), 1,
                    &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
  }
  if (imageIndex < m_imagesInFlight.size()) {
    m_imagesInFlight[imageIndex] = m_inFlightFences[currentFrameIndex];
  }
  return result;
}

void VulkanSwapchain::waitForFrame(u32 currentFrameIndex) const {
  vkWaitForFences(m_device.getLogicalDevice(), 1,
                  &m_inFlightFences[currentFrameIndex], VK_TRUE, UINT64_MAX);
}

void VulkanSwapchain::waitForAllFrames() const {
  if (m_inFlightFences.empty()) {
    return;
  }
  vkWaitForFences(m_device.getLogicalDevice(),
                  static_cast<u32>(m_inFlightFences.size()),
                  m_inFlightFences.data(), VK_TRUE, UINT64_MAX);
}

VkResult VulkanSwapchain::present(u32 currentFrameIndex, u32 imageIndex) {
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[currentFrameIndex];

  VkSwapchainKHR swapChains[] = {m_handle};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  return vkQueuePresentKHR(m_device.getPresentQueue(), &presentInfo);
}

VkSemaphore VulkanSwapchain::getImageAvailableSemaphore(u32 i) const {
  return m_imageAvailableSemaphores[i];
}
VkSemaphore VulkanSwapchain::getRenderFinishedSemaphore(u32 i) const {
  return m_renderFinishedSemaphores[i];
}
VkFence VulkanSwapchain::getInFlightFence(u32 i) const {
  return m_inFlightFences[i];
}

VulkanFrameBuffer &VulkanSwapchain::getFramebuffer(u32 index) {
  return *m_framebuffers[index];
}

VkFormat VulkanSwapchain::getImageFormat() const {
  return m_device.getSurfaceFormat().format;
}

} // namespace backend
} // namespace LX_core
