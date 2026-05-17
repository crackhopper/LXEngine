#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <optional>
#include <vector>

// FrameGraph v1 still uses legacy VkRenderPass/VkFramebuffer objects as the
// backend attachment contract. Dynamic Rendering is a future backend cleanup,
// not the current execution path, so this wrapper remains active for both
// swapchain and offscreen frame-graph passes.

namespace LX_core {
namespace backend {

class VulkanDevice;
/**
 * @brief 渲染通道描述（静态配置）
 */
class VulkanRenderPass {
  struct Token {};

public:
  VulkanRenderPass(Token, VulkanDevice &device,
                   std::optional<VkFormat> colorFormat,
                   std::optional<VkFormat> depthFormat,
                   bool presentColorFinalLayout);
  ~VulkanRenderPass();

  static std::unique_ptr<VulkanRenderPass>
  create(VulkanDevice &device, VkFormat colorFormat, VkFormat depthFormat) {
    return std::make_unique<VulkanRenderPass>(
        Token{}, device, std::optional<VkFormat>{colorFormat},
        std::optional<VkFormat>{depthFormat}, true);
  }

  static std::unique_ptr<VulkanRenderPass>
  create(VulkanDevice &device, std::optional<VkFormat> colorFormat,
         std::optional<VkFormat> depthFormat, bool presentColorFinalLayout) {
    return std::make_unique<VulkanRenderPass>(
        Token{}, device, colorFormat, depthFormat, presentColorFinalLayout);
  }

  void setClearColor(float r, float g, float b, float a);

  VkRenderPass getHandle() const { return m_renderPass; }
  const std::vector<VkClearValue> &getClearValues() const {
    return m_clearValues;
  }
  VkFormat getDepthFormat() const { return m_depthFormat; }
  bool hasColorAttachment() const { return m_hasColorAttachment; }
  bool hasDepthAttachment() const { return m_hasDepthAttachment; }

private:
  VulkanDevice &m_device;
  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
  bool m_hasColorAttachment = false;
  bool m_hasDepthAttachment = false;
  std::vector<VkClearValue> m_clearValues;
};

} // namespace backend
} // namespace LX_core
