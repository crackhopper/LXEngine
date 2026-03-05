#include "vulkan_commandbuffer.hpp"
#include "vulkan_framebuffer.hpp"
#include "vulkan_renderpass.hpp"
#include <array>
#include <stdexcept>
namespace LX_core {
void VulkanCommandBuffer::begin() {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;                  // Optional
  beginInfo.pInheritanceInfo = nullptr; // Optional
  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }
}

void VulkanCommandBuffer::end() {
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}

// 这两个配套使用，当是render pass时，调用这两个函数
void VulkanCommandBuffer::beginRenderPass(const RenderPassInfo &info) {
  begin();
  auto pVulkanRenderPass =
      std::dynamic_pointer_cast<VulkanRenderPass>(info.pRenderPass.lock());
  auto pVulkanFrameBuffer =
      std::dynamic_pointer_cast<VulkanFrameBuffer>(info.pFrameBuffer.lock());
  /***************** begin render pass */
  // 填充 render pass 启动配置
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = pVulkanRenderPass->getHandle();
  // 绑定帧
  renderPassInfo.framebuffer = pVulkanFrameBuffer->getHandle();
  // 设定渲染区域
  renderPassInfo.renderArea.offset = {info.renderArea.x, info.renderArea.y};
  renderPassInfo.renderArea.extent = {info.renderArea.width,
                                      info.renderArea.height};
  // 设定clear value
  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clearValues[1].depthStencil = {1.0f, 0};
  renderPassInfo.clearValueCount = clearValues.size();
  renderPassInfo.pClearValues = clearValues.data();

  // 启动render pass
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);
  currentRenderArea = info.renderArea;
}
void VulkanCommandBuffer::endRenderPass() {
  // 结束render pass
  vkCmdEndRenderPass(commandBuffer);
  end();
}

void VulkanCommandBuffer::setViewport(const Rect2Di* viewport) {
  if (viewport == nullptr) {
    viewport = &currentRenderArea;
  }

  VkViewport vkViewport{};
  vkViewport.x = viewport->x;
  vkViewport.y = viewport->y;
  vkViewport.width = viewport->width;
  vkViewport.height = viewport->height;
  vkViewport.minDepth = 0.0f;
  vkViewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &vkViewport);
}

void VulkanCommandBuffer::setScissor(const Rect2Di* scissor) {
  if (scissor == nullptr) {
    scissor = &currentRenderArea;
  }
  VkRect2D vkScissor{};
  vkScissor.offset = {scissor->x, scissor->y};
  vkScissor.extent = {scissor->width, scissor->height};
  vkCmdSetScissor(commandBuffer, 0, 1, &vkScissor);
}

void VulkanCommandBuffer::bindPipeline(const PipelineInfo &pipelineInfo) {
  auto pVulkanPipeline =
      std::dynamic_pointer_cast<VulkanPipeline>(pipelineInfo.pPipeline.lock());
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pVulkanPipeline->getHandle());
}

 void VulkanCommandBuffer::bindMeshList(const MeshListInfo &meshListInfo) {
  // for (auto &mesh : meshListInfo.pMeshList) {
  //   auto pVulkanMesh = std::dynamic_pointer_cast<VulkanMesh>(mesh.lock());
  //   pVulkanMesh->bind(commandBuffer);
  // }
  std::vector<VkBuffer> vertexBuffers;
  std::vector<VkDeviceSize> offsets;

  VkBuffer vertexBuffers[] = {vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);  
}

} // namespace LX_graphics