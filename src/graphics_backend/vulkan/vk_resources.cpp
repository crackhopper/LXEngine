#include "vk_resources.hpp"

namespace LX_core::graphic_backend {

VulkanBuffer::VulkanBuffer(VulkanDeviceWeakPtr device) : m_device(device) {}

VulkanBuffer::~VulkanBuffer() {
  auto dev = m_device.lock();
  if (!dev)
    return;

  VkDevice device = dev->getHandle();

  if (buffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, buffer, nullptr);

  if (memory != VK_NULL_HANDLE)
    vkFreeMemory(device, memory, nullptr);
}

VulkanImage::VulkanImage(VulkanDeviceWeakPtr device) : m_device(device) {}

VulkanImage::~VulkanImage() {
  auto dev = m_device.lock();
  if (!dev)
    return;

  VkDevice device = dev->getHandle();

  if (image != VK_NULL_HANDLE)
    vkDestroyImage(device, image, nullptr);

  if (memory != VK_NULL_HANDLE)
    vkFreeMemory(device, memory, nullptr);
}

VulkanImageView::VulkanImageView(VulkanDeviceWeakPtr device)
    : m_device(device) {}

VulkanImageView::~VulkanImageView() {
  auto dev = m_device.lock();
  if (!dev)
    return;

  VkDevice device = dev->getHandle();

  if (view != VK_NULL_HANDLE)
    vkDestroyImageView(device, view, nullptr);
}

VulkanSampler::VulkanSampler(VulkanDeviceWeakPtr device) : m_device(device) {}

VulkanSampler::~VulkanSampler() {
  auto dev = m_device.lock();
  if (!dev)
    return;

  VkDevice device = dev->getHandle();

  if (sampler != VK_NULL_HANDLE)
    vkDestroySampler(device, sampler, nullptr);
}

VulkanMesh::VulkanMesh(VulkanDeviceWeakPtr device) : m_device(device) {}

VulkanTexture::VulkanTexture(VulkanDeviceWeakPtr device) : m_device(device) {}

} // namespace LX_core::graphic_backend