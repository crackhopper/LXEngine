#pragma once
#include <map>
#include <memory>
#include <vector>
#include "vkc_cmdbuffer.hpp"
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

// VulkanDevice is fully defined via vkc_cmdbuffer.hpp -> vk_device.hpp

// 每帧的上下文，封装 Pool 和已经分配出的 Buffer
struct CommandFrameContext {
  VkCommandPool pool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> activeBuffers;
  uint32_t nextAvailableBuffer = 0;
};

class VulkanCommandBufferManager {
  struct Token {};

public:
  VulkanCommandBufferManager(Token, VulkanDevice &device,
                             uint32_t maxFramesInFlight,
                             uint32_t queueFamilyIndex);
  ~VulkanCommandBufferManager();

  static std::unique_ptr<VulkanCommandBufferManager>
  create(VulkanDevice &device, uint32_t maxFramesInFlight,
         uint32_t queueFamilyIndex) {
    return std::make_unique<VulkanCommandBufferManager>(
        Token{}, device, maxFramesInFlight, queueFamilyIndex);
  }

  void beginFrame(uint32_t currentFrameIndex);
  VulkanCommandBufferPtr allocateBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

  VulkanCommandBufferPtr beginSingleTimeCommands();
  void endSingleTimeCommands(VulkanCommandBufferPtr commandBuffer, VkQueue queue);

private:
  void createPool(VkCommandPool &pool, VkCommandPoolCreateFlags flags);

  VulkanDevice &m_device;
  uint32_t m_currentFrameIndex = 0;
  uint32_t m_maxFramesInFlight = 0;
  uint32_t m_queueFamilyIndex = 0;

  std::vector<CommandFrameContext> m_frameContexts;
  VkCommandPool m_transientPool = VK_NULL_HANDLE;
};

using VulkanCommandBufferManagerPtr = std::unique_ptr<VulkanCommandBufferManager>;

} // namespace backend
} // namespace LX_core