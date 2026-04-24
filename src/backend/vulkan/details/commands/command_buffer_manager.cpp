#include "command_buffer_manager.hpp"
#include "../device.hpp"
#include <stdexcept>

namespace LX_core {
namespace backend {

VulkanCommandBufferManager::VulkanCommandBufferManager(
    Token, VulkanDevice &device, u32 maxFramesInFlight, u32 queueFamilyIndex)
    : m_device(device), m_maxFramesInFlight(maxFramesInFlight),
      m_queueFamilyIndex(queueFamilyIndex) {
  m_frameContexts.resize(maxFramesInFlight);

  for (size_t i = 0; i < maxFramesInFlight; ++i) {
    createPool(m_frameContexts[i].pool, 0);
  }
  createPool(m_transientPool, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
}

VulkanCommandBufferManager::~VulkanCommandBufferManager() {
  VkDevice device = m_device.getLogicalDevice();

  for (size_t i = 0; i < m_frameContexts.size(); ++i) {
    if (m_frameContexts[i].pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, m_frameContexts[i].pool, nullptr);
      m_frameContexts[i].pool = VK_NULL_HANDLE;
    }
  }

  if (m_transientPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, m_transientPool, nullptr);
    m_transientPool = VK_NULL_HANDLE;
  }
}

void VulkanCommandBufferManager::createPool(VkCommandPool &pool, VkCommandPoolCreateFlags flags) {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = flags;
  poolInfo.queueFamilyIndex = m_queueFamilyIndex;

  if (vkCreateCommandPool(m_device.getLogicalDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create command pool!");
  }
}

void VulkanCommandBufferManager::beginFrame(u32 currentFrameIndex) {
  m_currentFrameIndex = currentFrameIndex;

  CommandFrameContext &frame = m_frameContexts[currentFrameIndex];
  vkResetCommandPool(m_device.getLogicalDevice(), frame.pool, 0);
  frame.activeBuffers.clear();
  frame.nextAvailableBuffer = 0;
}

VulkanCommandBufferUniquePtr VulkanCommandBufferManager::allocateBuffer(VkCommandBufferLevel level) {
  CommandFrameContext &frame = m_frameContexts[m_currentFrameIndex];

  if (frame.nextAvailableBuffer < frame.activeBuffers.size()) {
    VkCommandBuffer buffer = frame.activeBuffers[frame.nextAvailableBuffer++];
    vkResetCommandBuffer(buffer, 0);
    return std::make_unique<VulkanCommandBuffer>(buffer, m_device);
  }

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = frame.pool;
  allocInfo.level = level;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer buffer;
  if (vkAllocateCommandBuffers(m_device.getLogicalDevice(), &allocInfo, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffer!");
  }

  frame.activeBuffers.push_back(buffer);
  frame.nextAvailableBuffer++;
  return std::make_unique<VulkanCommandBuffer>(buffer, m_device);
}

VulkanCommandBufferUniquePtr VulkanCommandBufferManager::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_transientPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer buffer;
  if (vkAllocateCommandBuffers(m_device.getLogicalDevice(), &allocInfo, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate transient command buffer!");
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(buffer, &beginInfo);
  return std::make_unique<VulkanCommandBuffer>(buffer, m_device);
}

void VulkanCommandBufferManager::endSingleTimeCommands(VulkanCommandBufferUniquePtr commandBuffer, VkQueue queue) {
  VkCommandBuffer handle = commandBuffer->getHandle();
  vkEndCommandBuffer(handle);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &handle;

  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkFreeCommandBuffers(m_device.getLogicalDevice(), m_transientPool, 1, &handle);
}

} // namespace backend
} // namespace LX_core
