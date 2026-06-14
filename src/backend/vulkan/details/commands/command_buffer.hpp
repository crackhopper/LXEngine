#pragma once
#include "core/frame_graph/render_input.hpp"
#include "core/scene/scene.hpp"
#include "../device.hpp"
#include "../pipelines/graphics_pipeline.hpp"
#include "../pipelines/pipeline_ref.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <string_view>
#include <vector>

namespace LX_core {
struct RenderBatch;
struct RenderBatchGeometryResources;
struct RenderPathNodeContext;
} // namespace LX_core

namespace LX_core::backend {

[[nodiscard]] VkViewport makeVulkanViewport(u32 width, u32 height);

class VulkanResourceManager;
class VulkanComputePipeline;
class VulkanBuffer;

struct VulkanRenderBatchSubmissionStats final {
  usize compilerBatchCountConsumed = 0;
  usize boundBatchGeometryCount = 0;
  usize submittedDirectIndexedDrawCount = 0;
  usize submittedIndexedIndirectCommandCount = 0;
  usize submittedIndirectBatchCount = 0;
  usize submittedIndirectDrawCount = 0;
  u32 firstCommandOffset = 0;
  u32 lastCommandOffset = 0;
  usize fallbackObservedCount = 0;
};

class VulkanCommandBuffer {
public:
  VulkanCommandBuffer(
      VkCommandBuffer handle, VulkanDevice &device,
      std::vector<std::unique_ptr<VulkanBuffer>> *retainedIndirectBuffers =
          nullptr);
  ~VulkanCommandBuffer();

  VkCommandBuffer getHandle() const { return m_handle; }

  void begin();
  void end();

  void beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                       VkExtent2D extent,
                       const std::vector<VkClearValue> &clearValues);
  void endRenderPass() { vkCmdEndRenderPass(m_handle); }
  void beginRendering(VkExtent2D extent,
                      const std::vector<VkRenderingAttachmentInfo>
                          &colorAttachments,
                      const VkRenderingAttachmentInfo *depthAttachment,
                      u32 layerCount = 1);
  void endRendering() { vkCmdEndRendering(m_handle); }

  void setViewport(u32 width, u32 height);
  void setScissor(u32 width, u32 height);

  void bindPipeline(VulkanGraphicsPipeline &pipeline);
  void bindPipeline(VulkanComputePipeline &pipeline);
  void bindPipeline(VulkanPipelineRef pipeline);

  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanGraphicsPipeline &pipeline,
                     const RenderWorkItem &item);
  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanGraphicsPipeline &pipeline,
                     const RenderInput &input, const RenderInputDesc &desc);
  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanComputePipeline &pipeline,
                     const RenderWorkItem &item);
  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanComputePipeline &pipeline,
                     const RenderInput &input, const RenderInputDesc &desc);
  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanPipelineRef pipeline, const RenderWorkItem &item);
  void bindResources(VulkanResourceManager &resourceManager,
                     VulkanPipelineRef pipeline, const RenderInput &input,
                     const RenderInputDesc &desc);
  void bindSceneBindlessResources(VulkanResourceManager &resourceManager,
                                  VulkanPipelineRef pipeline,
                                  const RenderPathNodeContext &context,
                                  PipelineKey pipelineKey = {});
  void bindRenderBatchGeometry(VulkanResourceManager &resourceManager,
                               const RenderBatchGeometryResources &geometry);

  void executeWorkItem(const RenderWorkItem &item);
  void executeRenderInput(const RenderInput &input,
                          const RenderInputDesc &desc);
  void executeRenderBatch(const RenderBatch &batch);

  const VulkanRenderBatchSubmissionStats &
  getRenderBatchSubmissionStats() const {
    return m_renderBatchSubmissionStats;
  }

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
  void bindDescriptorResourcesWithLayout(
      VulkanResourceManager &resourceManager,
      const std::vector<ShaderResourceBinding> &bindings,
      VkPipelineLayout pipelineLayout, VkPipelineBindPoint bindPoint,
      const DescriptorResourceList &descriptorResources, StringID pass,
      PipelineKey pipelineKey, std::string_view operation);
  void
  bindResourcesWithLayout(VulkanResourceManager &resourceManager,
                          const std::vector<ShaderResourceBinding> &bindings,
                          VkPipelineLayout pipelineLayout,
                          VkPipelineBindPoint bindPoint,
                          const RenderWorkItem &item);
  void
  bindResourcesWithLayout(VulkanResourceManager &resourceManager,
                          const std::vector<ShaderResourceBinding> &bindings,
                          VkPipelineLayout pipelineLayout,
                          VkPipelineBindPoint bindPoint,
                          const RenderInput &input,
                          const RenderInputDesc &desc);
  void bindRenderInputGeometry(VulkanResourceManager &resourceManager,
                               const RenderInput &input);
  void executeDirectRasterPassItem(const RenderWorkItem &item);
  void executeComputeDispatchItem(const RenderWorkItem &item);

  VkCommandBuffer m_handle = VK_NULL_HANDLE;
  VulkanDevice &m_device;
  std::vector<std::unique_ptr<VulkanBuffer>> *m_retainedIndirectBuffers =
      nullptr;
  std::vector<std::unique_ptr<VulkanBuffer>> m_ownedIndirectBuffers;
  VulkanRenderBatchSubmissionStats m_renderBatchSubmissionStats;
  bool m_renderBatchGeometryBound = false;
};

using VulkanCommandBufferUniquePtr = std::unique_ptr<VulkanCommandBuffer>;

} // namespace LX_core::backend
