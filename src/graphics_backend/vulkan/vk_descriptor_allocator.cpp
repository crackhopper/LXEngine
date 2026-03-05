#include "vk_descriptor_allocator.hpp"
#include <stdexcept>

namespace LX_core::graphic_backend {

VulkanDescriptorAllocator::VulkanDescriptorAllocator(VulkanDeviceWeakPtr device)
    : m_device(device) {
  createPool();
}

VulkanDescriptorAllocator::~VulkanDescriptorAllocator() {
  auto dev = m_device.lock();
  if (!dev)
    return;

  VkDevice device = dev->getHandle();
  if (m_pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
  }
}

bool VulkanDescriptorAllocator::createPool() {
  auto dev = m_device.lock();
  if (!dev)
    return false;
  VkDevice device = dev->getHandle();

  VkDescriptorPoolSize poolSizes[2]{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[0].descriptorCount = m_samplerCount;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[1].descriptorCount = m_uniformCount;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets = m_maxSets;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = poolSizes;

  VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool);
  return res == VK_SUCCESS;
}

VkDescriptorSet
VulkanDescriptorAllocator::allocate(const VkDescriptorSetLayout *layouts,
                                    uint32_t count) {
  if (!m_pool)
    createPool();

  auto dev = m_device.lock();
  if (!dev)
    return VK_NULL_HANDLE;
  VkDevice device = dev->getHandle();

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_pool;
  allocInfo.descriptorSetCount = count;
  allocInfo.pSetLayouts = layouts;

  std::vector<VkDescriptorSet> sets(count);
  VkResult res = vkAllocateDescriptorSets(device, &allocInfo, sets.data());
  if (res != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate descriptor set");

  for (auto &s : sets)
    m_activeSets.insert(s);
  return sets[0];
}

void VulkanDescriptorAllocator::free(VkDescriptorSet set) {
  if (!set || m_activeSets.find(set) == m_activeSets.end())
    return;

  auto dev = m_device.lock();
  if (!dev)
    return;
  VkDevice device = dev->getHandle();

  vkFreeDescriptorSets(device, m_pool, 1, &set);
  m_activeSets.erase(set);
}

void VulkanDescriptorAllocator::updatePoolSizes(uint32_t samplerCount,
                                                uint32_t uniformCount) {
  m_samplerCount = samplerCount;
  m_uniformCount = uniformCount;
  resetPool();
}

void VulkanDescriptorAllocator::resetPool() {
  auto dev = m_device.lock();
  if (!dev || !m_pool)
    return;
  VkDevice device = dev->getHandle();
  vkDestroyDescriptorPool(device, m_pool, nullptr);
  m_pool = VK_NULL_HANDLE;
  m_activeSets.clear();
  createPool();
}

} // namespace LX_core::graphic_backend