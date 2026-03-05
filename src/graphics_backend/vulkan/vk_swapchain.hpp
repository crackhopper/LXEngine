#pragma once
#include "vk_device.hpp"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {

class VulkanSwapchain {
public:
  VulkanSwapchain() = default;
  ~VulkanSwapchain();

  void initialize(VulkanDevice *device, VkSurfaceKHR surface, uint32_t width,
                  uint32_t height);

  void shutdown();

  // 获取 swapchain 相关数据
  VkSwapchainKHR getHandle() const { return swapchain; }

  const std::vector<VkImage> &getImages() const { return images; }

  const std::vector<VkImageView> &getImageViews() const { return imageViews; }

  VkExtent2D getExtent() const { return extent; }

  VkFormat getFormat() const { return format; }

private:
  void createSwapchain();
  void createImageViews();

private:
  VulkanDevice *p_device = nullptr;
  VkSurfaceKHR surface = VK_NULL_HANDLE;

  uint32_t width = 0;
  uint32_t height = 0;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;

  std::vector<VkImage> images;
  std::vector<VkImageView> imageViews;

  VkExtent2D extent{};
  VkFormat format{};
};

using VulkanSwapchainPtr = std::shared_ptr<VulkanSwapchain>;

} // namespace LX_core::graphic_backend