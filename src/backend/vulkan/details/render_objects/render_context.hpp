#pragma once
#include <vulkan/vulkan.h>

namespace LX_core::backend {

class VulkanRenderPass;
class VulkanFrameBuffer;

/**
 * @brief 渲染上下文：通过引用强制绑定必要资源
 */
struct VulkanRenderContext {
  VulkanRenderPass &renderPass;
  VulkanFrameBuffer &framebuffer;

  VkExtent2D renderExtent;
  VkOffset2D renderOffset = {0, 0};

  // 构造函数强制要求传入有效引用
  VulkanRenderContext(VulkanRenderPass &rp, VulkanFrameBuffer &fb,
                      VkExtent2D extent);
};

} // namespace LX_core::backend