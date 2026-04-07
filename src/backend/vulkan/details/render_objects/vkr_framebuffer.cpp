#include "vkr_framebuffer.hpp"
#include "../vk_device.hpp"
#include <stdexcept>

namespace LX_core {
namespace backend {

VulkanFrameBuffer::VulkanFrameBuffer(Token, VulkanDevice &device,
                                   VkRenderPass renderPass,
                                   const std::vector<VkImageView> &attachments,
                                   VkExtent2D extent)
    : m_device(device), m_extent(extent) {
  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = renderPass;
  framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  framebufferInfo.pAttachments = attachments.data();
  framebufferInfo.width = extent.width;
  framebufferInfo.height = extent.height;
  framebufferInfo.layers = 1;

  if (vkCreateFramebuffer(m_device.getLogicalDevice(), &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create framebuffer!");
  }
}

VulkanFrameBuffer::~VulkanFrameBuffer() {
  if (m_framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(m_device.getLogicalDevice(), m_framebuffer, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
  }
}

} // namespace backend
} // namespace LX_core