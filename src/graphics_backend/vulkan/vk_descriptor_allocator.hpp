#pragma once
#include "vk_device.hpp"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <unordered_set>

namespace LX_core::graphic_backend {

using VulkanDeviceWeakPtr = std::weak_ptr<VulkanDevice>;

class VulkanDescriptorAllocator {
public:
  VulkanDescriptorAllocator(VulkanDeviceWeakPtr device);
  ~VulkanDescriptorAllocator();

  // 分配 descriptor set
  VkDescriptorSet allocate(const VkDescriptorSetLayout *layouts,
                           uint32_t count);

  // 回收 descriptor set
  void free(VkDescriptorSet set);
  // 更新 pool size，例如增加 sampler 或 uniform buffer 数量
  void updatePoolSizes(uint32_t samplerCount, uint32_t uniformCount);

  // 可选：重置 pool
  void resetPool();

private:
  VulkanDeviceWeakPtr m_device;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;

  uint32_t m_maxSets = 1000; // 默认最大 descriptor set 数量
  uint32_t m_samplerCount = 1000;
  uint32_t m_uniformCount = 1000;

  std::unordered_set<VkDescriptorSet> m_activeSets;
  bool createPool();
};

} // namespace LX_core::graphic_backend