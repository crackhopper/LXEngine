#pragma once

#include <memory>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

class VulkanDevice;
class VulkanCommandBuffer;


// 这个资源是一个 Vulkan 缓冲区，用于存储：
// - 顶点数据
// - 索引数据
// - Uniform 数据
class VulkanBuffer;
using VulkanBufferUniquePtr = std::unique_ptr<VulkanBuffer>;
class VulkanBuffer {
  struct Token {};

public:
  VulkanBuffer(Token token, VulkanDevice &device, VkDeviceSize size,
               VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
  ~VulkanBuffer();

  static VulkanBufferUniquePtr create(VulkanDevice &device, VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties) {
    return std::make_unique<VulkanBuffer>(Token{}, device, size, usage,
                                          properties);
  }

  void *map();
  void unmap();

  void uploadData(const void *data, VkDeviceSize dataSize);

  void copyTo(VkCommandBuffer cmdBuffer, VulkanBuffer &dst);

  VkBuffer getHandle() const { return m_buffer; }
  VkDeviceMemory getMemory() const { return m_memory; }
  VkDeviceSize getSize() const { return m_size; }

private:
  VkDevice m_device = VK_NULL_HANDLE;
  VkBuffer m_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkDeviceSize m_size = 0;
  VkBufferUsageFlags m_usage;
  void *m_mappedPtr = nullptr;
};
} // namespace backend
} // namespace LX_core