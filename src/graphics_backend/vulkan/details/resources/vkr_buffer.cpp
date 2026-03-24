#include "vkr_buffer.hpp"
#include "../vk_device.hpp"
#include <cstring>
#include <stdexcept>

namespace LX_core {
namespace graphic_backend {

VulkanBuffer::VulkanBuffer(Token, VulkanDevice &device, VkDeviceSize size,
                         VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
    : m_device(device.getLogicalDevice()), m_size(size), m_usage(usage) {
  // Create buffer
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create buffer!");
  }

  // Allocate memory
  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(m_device, m_buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = device.findMemoryTypeIndex(
      memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate buffer memory!");
  }

  vkBindBufferMemory(m_device, m_buffer, m_memory, 0);
}

VulkanBuffer::~VulkanBuffer() {
  if (m_device != VK_NULL_HANDLE) {
    if (m_buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(m_device, m_buffer, nullptr);
      m_buffer = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
      vkFreeMemory(m_device, m_memory, nullptr);
      m_memory = VK_NULL_HANDLE;
    }
  }
}

void *VulkanBuffer::map() {
  if (m_mappedPtr != nullptr) {
    return m_mappedPtr;
  }
  vkMapMemory(m_device, m_memory, 0, m_size, 0, &m_mappedPtr);
  return m_mappedPtr;
}

void VulkanBuffer::unmap() {
  if (m_mappedPtr != nullptr) {
    vkUnmapMemory(m_device, m_memory);
    m_mappedPtr = nullptr;
  }
}

void VulkanBuffer::uploadData(const void *data, VkDeviceSize dataSize) {
  // For HOST_VISIBLE memory, we can map and copy directly
  void *mapped = map();
  std::memcpy(mapped, data, static_cast<size_t>(dataSize));
  unmap();
}

void VulkanBuffer::copyTo(VkCommandBuffer cmdBuffer, VulkanBuffer &dst) {
  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = m_size;
  vkCmdCopyBuffer(cmdBuffer, m_buffer, dst.getHandle(), 1, &copyRegion);
}

} // namespace graphic_backend
} // namespace LX_core
