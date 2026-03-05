#include "vk_framebuffer.hpp"
#include <stdexcept>

namespace LX_core::graphic_backend {

VulkanFramebuffer::~VulkanFramebuffer() {}

void VulkanFramebuffer::create(VulkanDevicePtr device, VkRenderPass renderPass,
                               const VkImageView *attachments,
                               uint32_t attachmentCount, VkExtent2D extent) {

  VkFramebufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  info.renderPass = renderPass;
  info.attachmentCount = attachmentCount;
  info.pAttachments = attachments;
  info.width = extent.width;
  info.height = extent.height;
  info.layers = 1;

  if (vkCreateFramebuffer(device->getHandle(), &info, nullptr, &framebuffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create framebuffer");
  }
}

void VulkanFramebuffer::destroy(VulkanDevicePtr device) {

  if (framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device->getHandle(), framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
  }
}

} // namespace LX_core::graphic_backend