#pragma once
#include "core/platform/window.hpp"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

class VulkanDevice;
class VulkanRenderPass;
class VulkanFrameBuffer;

/**
 * @brief 交换链封装类
 * 管理显示图像队列、渲染目标同步以及 Framebuffer 的生命周期
 */
class VulkanSwapchain {
  struct Token {};

public:
  VulkanSwapchain(Token, VulkanDevice &device, WindowSharedPtr window,
                  u32 maxFramesInFlight = 3);
  ~VulkanSwapchain();

  static std::unique_ptr<VulkanSwapchain>
  create(VulkanDevice &device, WindowSharedPtr window,
         u32 maxFramesInFlight = 3) {
    return std::make_unique<VulkanSwapchain>(Token{}, device, window,
                                             maxFramesInFlight);
  }

  // --- 核心生命周期控制 ---
  void initialize(VulkanRenderPass &renderPass);
  void rebuild(VulkanRenderPass &renderPass);

  // --- 同步对象获取 ---
  VkSemaphore getImageAvailableSemaphore(u32 currentFrameIndex) const;
  VkSemaphore getRenderFinishedSemaphore(u32 currentFrameIndex) const;
  VkFence getInFlightFence(u32 currentFrameIndex) const;

  // --- 帧获取与呈现 ---
  VkResult acquireNextImage(u32 currentFrameIndex, u32 &imageIndex);
  VkResult present(u32 currentFrameIndex, u32 imageIndex);

  // --- 资源访问 ---
  VkSwapchainKHR getHandle() const { return m_handle; }
  VkExtent2D getExtent() const { return m_extent; }
  VulkanFrameBuffer &getFramebuffer(u32 index);
  usize getImageCount() const { return m_images.size(); }
  VkFormat getImageFormat() const;
  VkImageView getDepthImageView() const { return m_depthImageView; }

  // --- 辅助函数 ---
  void waitIdle() const;

private:
  void cleanup();
  void createInternal(VkExtent2D extent);
  void createImageViews();
  void createDepthResources();
  void createSyncObjects();
  void setupFramebuffers(VulkanRenderPass &renderPass);

  VulkanDevice &m_device;
  WindowSharedPtr m_window;
  u32 m_maxFramesInFlight = 3;

  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkSwapchainKHR m_handle = VK_NULL_HANDLE;
  
  VkSurfaceFormatKHR m_surfaceFormat = {};
  VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
  VkImageAspectFlags m_depthAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  VkExtent2D m_extent{};

  std::vector<VkImage> m_images;
  std::vector<VkImageView> m_imageViews;

  // 深度资源
  VkImage m_depthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
  VkImageView m_depthImageView = VK_NULL_HANDLE;

  // Framebuffers
  std::vector<std::unique_ptr<VulkanFrameBuffer>> m_framebuffers;

  // 同步对象
  std::vector<VkSemaphore> m_imageAvailableSemaphores;
  std::vector<VkSemaphore> m_renderFinishedSemaphores;
  std::vector<VkFence> m_inFlightFences;
};

using VulkanSwapchainUniquePtr = std::unique_ptr<VulkanSwapchain>;

} // namespace backend
} // namespace LX_core
