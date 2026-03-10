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

// 这个类是渲染输出的目标
namespace LX_core::graphic_backend {

// 渲染流程。比较复杂，仅仅pipeline是不够的。
// renderPass的抽象主要负责：
// - 描述渲染输出的目标（颜色附件、深度附件）
// - 描述渲染过程中如何处理这些目标（加载操作、存储操作）
// - 描述附件的初始布局和最终布局（用于布局转换）
// -
// 管理subpass（目前仅支持1个subpass），这个能力移动端更需要。提升流水线并行度，以及多个subpass间依赖描述方便硬件侧优化。
class VulkanRenderPass;
using VulkanRenderPassPtr = std::shared_ptr<VulkanRenderPass>;
class VulkanRenderPass;
using VulkanRenderPassPtr = std::shared_ptr<VulkanRenderPass>;

/**
 * 简化 Vulkan RenderPass 接口
 * - 内部固定常用 loadOp/storeOp/layout
 * - 提供几种常用模式创建函数
 * - 适合 swapchain / offscreen / gbuffer 等常见场景
 */
/* 例子：创建一个离屏 pass，2 个 color attachment + depth
void createOffscreenPass(VulkanDevicePtr device) {

    auto offscreenPass = VulkanRenderPass::createPtr(
        device,
        VulkanRenderPass::Type::Offscreen,
        {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}, // color
attachments VK_FORMAT_D32_SFLOAT        // depth format
    );

    VkRenderPass handle = offscreenPass->getHandle();
}
*/
class VulkanRenderPass {
public:
  enum class Type {
    Swapchain, // 渲染到屏幕
    Offscreen, // 渲染到离屏纹理
    DepthOnly, // 仅深度
    GBuffer    // Deferred shading GBuffer
  };

  // 禁止直接构造带繁琐参数
  VulkanRenderPass() = delete;

  /**
   * 构造通用模式
   * @param device VulkanDevice
   * @param type 常用类型
   * @param colorAttachmentCount color attachment 数量
   * (只有Swapchain/Offscreen/GBuffer有意义)
   * @param depthFormat 可选深度格式
   */
  VulkanRenderPass(
      VulkanDevicePtr device, Type type,
      std::vector<VkFormat> colorFormats, // 颜色附件格式，同时也指定了数量
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);

  ~VulkanRenderPass();

  static VulkanRenderPassPtr createPtr(
      VulkanDevicePtr device, Type type,
      std::vector<VkFormat> colorFormats, // 颜色附件格式，同时也指定了数量
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT) {
    return std::make_shared<VulkanRenderPass>(device, type, colorFormats,
                                              depthFormat);
  }

  VkRenderPass getHandle() const { return mRenderPass; }

private:
  VulkanDevicePtr mDevice = nullptr;
  VkRenderPass mRenderPass = VK_NULL_HANDLE;

  void init(Type type, std::vector<VkFormat> colorFormats,
            VkFormat depthFormat);

  // 内部封装常用 attachment 配置
  VkAttachmentLoadOp getDefaultLoadOp(Type type) const;
  VkAttachmentStoreOp getDefaultStoreOp(Type type) const;
  VkImageLayout getDefaultInitialLayout(Type type) const;
  VkImageLayout getDefaultFinalLayout(Type type) const;
};

class VulkanFramebuffer {
public:
  VulkanFramebuffer() = default;
  ~VulkanFramebuffer();

  /**
   * @param device Vulkan 设备
   * @param renderPass VulkanRenderPass 对象
   * @param attachments VulkanImageView 指针数组（color + depth）
   * @param extent framebuffer 尺寸
   */
  void create(VulkanDevicePtr device, VulkanRenderPassPtr renderPass,
              const std::vector<VulkanImageView *> &attachments,
              VkExtent2D extent);

  void destroy(VulkanDevicePtr device);

  VkFramebuffer getHandle() const { return framebuffer; }

private:
  VkFramebuffer framebuffer = VK_NULL_HANDLE;

  // 保存内部状态，方便调试/后续操作
  VulkanRenderPassPtr mRenderPass = nullptr;
  std::vector<VulkanImageView *> mAttachments;
  VkExtent2D mExtent{};
};

} // namespace LX_core::graphic_backend