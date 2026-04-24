#pragma once
#include <map>
#include <memory>
#include <vector>
#include "command_buffer.hpp"
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

// VulkanDevice is fully defined via command_buffer.hpp -> device.hpp

// 每帧的上下文，封装 Pool 和已经分配出的 Buffer
struct CommandFrameContext {
  VkCommandPool pool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> activeBuffers;
  FrameIndex32 nextAvailableBuffer = 0;
};

class VulkanCommandBufferManager {
  struct Token {};

public:
  VulkanCommandBufferManager(Token, VulkanDevice &device,
                             FrameIndex32 maxFramesInFlight,
                             QueueFamilyIndex32 queueFamilyIndex);
  ~VulkanCommandBufferManager();

  static std::unique_ptr<VulkanCommandBufferManager>
  create(VulkanDevice &device, FrameIndex32 maxFramesInFlight,
         QueueFamilyIndex32 queueFamilyIndex) {
    return std::make_unique<VulkanCommandBufferManager>(
        Token{}, device, maxFramesInFlight, queueFamilyIndex);
  }

  void beginFrame(FrameIndex32 currentFrameIndex);
  VulkanCommandBufferUniquePtr allocateBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

  VulkanCommandBufferUniquePtr beginSingleTimeCommands();
  void endSingleTimeCommands(VulkanCommandBufferUniquePtr commandBuffer, VkQueue queue);

private:
  void createPool(VkCommandPool &pool, VkCommandPoolCreateFlags flags);

  VulkanDevice &m_device;
  FrameIndex32 m_currentFrameIndex = 0;
  FrameIndex32 m_maxFramesInFlight = 0;
  QueueFamilyIndex32 m_queueFamilyIndex = 0;

  std::vector<CommandFrameContext> m_frameContexts;
  VkCommandPool m_transientPool = VK_NULL_HANDLE;
};

using VulkanCommandBufferManagerUniquePtr = std::unique_ptr<VulkanCommandBufferManager>;

} // namespace backend
} // namespace LX_core
