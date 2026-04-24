#pragma once
#include "core/scene/scene.hpp"
#include "../pipelines/pipeline.hpp"
#include "../device.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace LX_core::backend {

class VulkanResourceManager;

class VulkanCommandBuffer {
public:
  VulkanCommandBuffer(VkCommandBuffer handle, VulkanDevice &device)
      : m_handle(handle), m_device(device) {}
  ~VulkanCommandBuffer() = default;

  VkCommandBuffer getHandle() const { return m_handle; }

  void begin();
  void end();

  void beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                       VkExtent2D extent,
                       const std::vector<VkClearValue> &clearValues);
  void endRenderPass() { vkCmdEndRenderPass(m_handle); }

  void setViewport(u32 width, u32 height);
  void setScissor(u32 width, u32 height);

  void bindPipeline(VulkanPipeline &pipeline);

  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanPipeline &pipeline, const RenderingItem &item);

  void drawItem(const RenderingItem &item);

  void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size,
                  VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(m_handle, src, dst, 1, &copyRegion);
  }

  void copyBufferToImage(VkBuffer src, VkImage dst, u32 width, u32 height) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(m_handle, src, dst,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }

  void pipelineBarrier(VkPipelineStageFlags srcStage,
                       VkPipelineStageFlags dstStage,
                       VkImageMemoryBarrier barrier) {
    vkCmdPipelineBarrier(m_handle, srcStage, dstStage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
  }

private:
  // Push constant info captured from the last bound pipeline. Matches the
  // engine-wide convention set in `PushConstantRange` (128 bytes,
  // vertex+fragment stages by default); populated in `bindPipeline`.
  struct PushConstantSnapshot {
    VkShaderStageFlags stageFlags = 0;
    u32 offset = 0;
    u32 size = 0;
  };

  VkCommandBuffer m_handle = VK_NULL_HANDLE;
  VulkanDevice &m_device;

  // Captured from the last bound pipeline; used by drawItem().
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  PushConstantSnapshot m_pushConstants{};
};

using VulkanCommandBufferUniquePtr = std::unique_ptr<VulkanCommandBuffer>;

} // namespace LX_core::backend
