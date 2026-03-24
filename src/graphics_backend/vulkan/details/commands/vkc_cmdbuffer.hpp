#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include "core/scene/scene.hpp"
#include "../vk_device.hpp"
#include "../pipelines/vkp_pipeline.hpp"

namespace LX_core::graphic_backend {

class VulkanResourceManager;

class VulkanCommandBuffer {
public:
  VulkanCommandBuffer(VkCommandBuffer handle, VulkanDevice &device)
      : m_handle(handle), m_device(&device) {}
  ~VulkanCommandBuffer() = default;

  VkCommandBuffer getHandle() const { return m_handle; }

  void setResourceManager(VulkanResourceManager &mgr) {
    m_resourceManager = &mgr;
  }

  void beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                      VkExtent2D extent, const std::vector<VkClearValue> &clearValues);
  void endRenderPass() { vkCmdEndRenderPass(m_handle); }

  void setViewport(uint32_t width, uint32_t height);
  void setScissor(uint32_t width, uint32_t height);

  void bindPipeline(VulkanPipelineBase &pipeline);

  void bindResources(VulkanPipelineBase &pipeline, const RenderItem &item);

  void drawItem(const RenderItem &item);

  void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size,
                  VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(m_handle, src, dst, 1, &copyRegion);
  }

  void copyBufferToImage(VkBuffer src, VkImage dst, uint32_t width,
                         uint32_t height) {
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
    vkCmdPipelineBarrier(m_handle, srcStage, dstStage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
  }

private:
  VkCommandBuffer m_handle = VK_NULL_HANDLE;
  class VulkanDevice *m_device = nullptr;
  VulkanResourceManager *m_resourceManager = nullptr;

  // Captured from the last bound pipeline; used by drawItem().
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  PushConstantDetails m_pushConstantsDetails{};
};

} // namespace LX_core::graphic_backend