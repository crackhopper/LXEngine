#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

// 注意：在Dynamic Rendering时代，framebuffer和renderpass都是不需要的。
// 我们的项目从 vulkan tutorial 比较老的版本迁移过来，因此还用了这些旧特性；
// TODO: 后续迭代到设计 FrameGraph/RenderGraph 的时候，启用Dynamic
// Rendering。不再使用这个文件的内容。
// 因此这里的文件和流程，我们简单封装。不暴露太多依赖。

namespace LX_core {
namespace graphic_backend {

class VulkanDevice;
/**
 * @brief 渲染通道描述（静态配置）
 */
class VulkanRenderPass {
  struct Token {};

public:
  VulkanRenderPass(Token, VulkanDevice &device, VkFormat colorFormat, VkFormat depthFormat);
  ~VulkanRenderPass();

  static std::unique_ptr<VulkanRenderPass> create(VulkanDevice &device,
                                                  VkFormat colorFormat,
                                                  VkFormat depthFormat) {
    return std::make_unique<VulkanRenderPass>(Token{}, device, colorFormat, depthFormat);
  }

  void setClearColor(float r, float g, float b, float a);

  VkRenderPass getHandle() const { return m_renderPass; }
  const std::vector<VkClearValue> &getClearValues() const { return m_clearValues; }
  VkFormat getDepthFormat() const { return m_depthFormat; }

private:
  VulkanDevice *m_device = nullptr;
  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
  std::vector<VkClearValue> m_clearValues{2}; // 0: Color, 1: Depth
};


} // namespace graphic_backend
} // namespace LX_core
