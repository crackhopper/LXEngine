#pragma once
#include "vk_device.hpp"
#include "vk_resources.hpp"
#include <vector>
#include <vulkan/vulkan.h>

// 注意：在Dynamic Rendering时代，framebuffer和renderpass都是不需要的。
// 我们的项目从 vulkan tutorial 比较老的版本迁移过来，因此还用了这些旧特性；
// TODO: 后续迭代到设计 FrameGraph/RenderGraph 的时候，启用Dynamic
// Rendering。不再使用这个文件的内容。
// 因此这里的文件和流程，我们简单封装。不暴露太多依赖。

namespace LX_core::graphic_backend {

class VulkanRenderPass;
using VulkanRenderPassPtr = std::unique_ptr<VulkanRenderPass>;

class VulkanRenderPass {
public:
  enum class Type {
    Swapchain, // 渲染到屏幕
    Offscreen, // 渲染到离屏纹理
    DepthOnly, // 仅深度
    GBuffer    // Deferred shading GBuffer
  };

  VulkanRenderPass() = delete;

  VulkanRenderPass(
      VulkanDevice &device, Type type,
      std::vector<VkFormat> colorFormats,
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);

  ~VulkanRenderPass();

  static VulkanRenderPassPtr createPtr(
      VulkanDevice &device, Type type,
      std::vector<VkFormat> colorFormats,
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT) {
    return std::make_unique<VulkanRenderPass>(device, type, colorFormats,
                                              depthFormat);
  }

  VkRenderPass getHandle() const { return mRenderPass; }

private:
  VulkanDevice &mDevice;
  VkRenderPass mRenderPass = VK_NULL_HANDLE;

  void init(Type type, std::vector<VkFormat> colorFormats,
            VkFormat depthFormat);

  VkAttachmentLoadOp getDefaultLoadOp(Type type) const;
  VkAttachmentStoreOp getDefaultStoreOp(Type type) const;
  VkImageLayout getDefaultInitialLayout(Type type) const;
  VkImageLayout getDefaultFinalLayout(Type type) const;
};

class VulkanFramebuffer {
public:
  VulkanFramebuffer() = default;
  ~VulkanFramebuffer();

  void create(VulkanDevice &device, VkRenderPass renderPass,
              const VkImageView *attachments, uint32_t attachmentCount,
              VkExtent2D extent);

  void destroy(VulkanDevice &device);

  VkFramebuffer getHandle() const { return framebuffer; }

private:
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

} // namespace LX_core::graphic_backend
