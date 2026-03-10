#include "vk_cmdbuffer.hpp"
#include <array>
#include <stdexcept>
#include "../vk_mesh.hpp"

namespace LX_core::graphic_backend {

void VulkanCommandBuffer::begin() {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;                  // Optional
  beginInfo.pInheritanceInfo = nullptr; // Optional
  if (vkBeginCommandBuffer(_handle, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }
}

void VulkanCommandBuffer::end() {
  if (vkEndCommandBuffer(_handle) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}

void VulkanCommandBuffer::beginRenderPass(const RenderPassInfo &info) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = info.renderPass;
  // 绑定帧
  renderPassInfo.framebuffer = info.framebuffer;
  // 设定渲染区域
  renderPassInfo.renderArea.offset = info.offset;
  renderPassInfo.renderArea.extent = info.extent;
  // 设定clear value
  std::array<VkClearValue, 2> clearValues{};
  clearValues[0] = info.clearColor;
  clearValues[1] = info.clearDepth;
  renderPassInfo.clearValueCount = clearValues.size();
  renderPassInfo.pClearValues = clearValues.data();

  // 启动render pass
  vkCmdBeginRenderPass(_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::endRenderPass() {
  // 结束render pass
  vkCmdEndRenderPass(_handle);
}

void VulkanCommandBuffer::bindPipeline(const PipelineInfo &info) {
  vkCmdBindPipeline(_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline);
}

void VulkanCommandBuffer::bindMesh(const VulkanMesh &mesh) {
  VkBuffer vertexBuffers[] = {mesh.getVertexBuffer().getHandle()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(_handle, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(_handle, mesh.getIndexBuffer().getHandle(), 0,
                       VK_INDEX_TYPE_UINT32);
}

void VulkanCommandBuffer::drawMesh(const VulkanMesh &mesh) {
  vkCmdDrawIndexed(_handle, mesh.getIndexCount(), 1, 0, 0, 0);
} 

void VulkanCommandBuffer::setViewport(const VkViewport *viewport) {
  vkCmdSetViewport(_handle, 0, 1, viewport);
}

void VulkanCommandBuffer::setScissor(const VkRect2D *scissor ) {
  vkCmdSetScissor(_handle, 0, 1, scissor);
}

void VulkanCommandBuffer::bindMaterial(const VulkanMaterial &material) {
  // 绑定材质
}

} // namespace LX_core::graphic_backend
