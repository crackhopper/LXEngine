#pragma once

#include "vk_device.hpp"
#include "vk_resources.hpp"
#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {
class VulkanCommandBuffer;
using VulkanCommandBufferPtr = std::unique_ptr<VulkanCommandBuffer>;

class VulkanMesh;
class VulkanMaterial;

class VulkanCommandBuffer {
  struct Token {};

public:
  VulkanCommandBuffer(
      Token, VkDevice device, VkCommandPool commandPool,
      VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      VkCommandBufferUsageFlags flags =
          VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  static VulkanCommandBufferPtr
  create(VkDevice device, VkCommandPool commandPool,
         VkCommandBufferUsageFlags flags =
             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
         VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
    return std::make_unique<VulkanCommandBuffer>(Token{}, device, commandPool,
                                                 level, flags);
  }

  struct RenderPassInfo {
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkExtent2D extent;
    VkOffset2D offset = {0, 0};
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkClearValue clearDepth = {1.0f, 0};
  };

  struct PipelineInfo {
    VkPipeline pipeline;
    VkPipelineLayout layout; // 绑定 DescriptorSet 时需要
  };

  // 启用 RTTI
  virtual ~VulkanCommandBuffer();

  // 获取句柄
  VkCommandBuffer getHandle() const { return _handle; }

  // --- 核心状态切换 ---
  void begin();
  void end();

  // --- Render Pass 相关 ---
  void beginRenderPass(const RenderPassInfo &info);
  void endRenderPass();

  void setViewport(const VkViewport *viewport = nullptr);
  void setScissor(const VkRect2D *scissor = nullptr);

  // --- 资源绑定与绘制 ---
  void bindPipeline(const PipelineInfo &info);
  void bindMaterial(const VulkanMaterial &material);
  void bindMesh(const VulkanMesh &mesh);

  void drawMesh(const VulkanMesh &mesh);

protected:
  // 存储原始句柄以保证析构时的绝对安全
  VkDevice _rawDevice;
  VkCommandPool _rawPool;
  VkCommandBuffer _handle{VK_NULL_HANDLE};
};

using VulkanCommandBufferPtr = std::shared_ptr<VulkanCommandBuffer>;

} // namespace LX_core::graphic_backend