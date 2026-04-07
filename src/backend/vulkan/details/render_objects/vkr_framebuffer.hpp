#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace LX_core {
namespace backend {

class VulkanDevice;
/**
 * @brief 帧缓冲包装（RAII，随 Swapchain 或 RenderTarget 销毁）
 */
class VulkanFrameBuffer {
  struct Token {};

public:
  VulkanFrameBuffer(Token, VulkanDevice &device, VkRenderPass renderPass,
                    const std::vector<VkImageView> &attachments,
                    VkExtent2D extent);
  ~VulkanFrameBuffer();

  static std::unique_ptr<VulkanFrameBuffer> create(VulkanDevice &device,
                                                   VkRenderPass renderPass,
                                                   const std::vector<VkImageView> &attachments,
                                                   VkExtent2D extent) {
    return std::make_unique<VulkanFrameBuffer>(Token{}, device, renderPass, attachments, extent);
  }

  VkFramebuffer getHandle() const { return m_framebuffer; }
  VkExtent2D getExtent() const { return m_extent; }

private:
  VulkanDevice &m_device;
  VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
  VkExtent2D m_extent{};
};

} // namespace backend
} // namespace LX_core