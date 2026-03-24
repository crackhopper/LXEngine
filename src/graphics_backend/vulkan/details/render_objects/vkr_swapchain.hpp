#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace graphic_backend {

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
  VulkanSwapchain(Token, VulkanDevice &device, VkSurfaceKHR surface,
                  VkExtent2D extent, uint32_t graphicsIdx, uint32_t presentIdx, uint32_t maxFramesInFlight = 3);
  ~VulkanSwapchain();

  static std::unique_ptr<VulkanSwapchain>
  create(VulkanDevice &device, VkSurfaceKHR surface, VkExtent2D extent, uint32_t graphicsIdx, uint32_t presentIdx, uint32_t maxFramesInFlight = 3) {
    return std::make_unique<VulkanSwapchain>(Token{}, device, surface, extent, graphicsIdx, presentIdx, maxFramesInFlight);
  }

  // --- 核心生命周期控制 ---
  void initialize(VulkanRenderPass &renderPass);
  void rebuild(VkExtent2D newExtent, VulkanRenderPass &renderPass);

  // --- 同步对象获取 ---
  VkSemaphore getImageAvailableSemaphore(uint32_t currentFrameIndex) const;
  VkSemaphore getRenderFinishedSemaphore(uint32_t currentFrameIndex) const;
  VkFence getInFlightFence(uint32_t currentFrameIndex) const;

  // --- 帧获取与呈现 ---
  VkResult acquireNextImage(uint32_t currentFrameIndex, uint32_t &imageIndex);
  VkResult present(uint32_t currentFrameIndex, uint32_t imageIndex);

  // --- 资源访问 ---
  VkSwapchainKHR getHandle() const { return m_handle; }
  VkExtent2D getExtent() const { return m_extent; }
  VulkanFrameBuffer &getFramebuffer(uint32_t index);
  uint32_t getImageCount() const { return static_cast<uint32_t>(m_images.size()); }
  VkFormat getImageFormat() const { return m_imageFormat; }
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

  VulkanDevice *m_device = nullptr;
  uint32_t m_maxFramesInFlight = 3;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;

  VkSwapchainKHR m_handle = VK_NULL_HANDLE;
  VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
  VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
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

using VulkanSwapchainPtr = std::unique_ptr<VulkanSwapchain>;

} // namespace graphic_backend
} // namespace LX_core