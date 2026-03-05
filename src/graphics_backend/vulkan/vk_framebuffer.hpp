#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include "vk_device.hpp"

// 这个类是渲染输出的目标
namespace LX_core::graphic_backend {

class VulkanFramebuffer {
public:
  VulkanFramebuffer() = default;
  ~VulkanFramebuffer();

  void create(VulkanDevicePtr device, VkRenderPass renderPass,
              const VkImageView *attachments, uint32_t attachmentCount,
              VkExtent2D extent);

  void destroy(VulkanDevicePtr device);

  VkFramebuffer getHandle() const { return framebuffer; }

private:
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

} // namespace LX_core::graphic_backend