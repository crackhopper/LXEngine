#include "vulkan_renderer.hpp"

namespace LX_core {

void VulkanRenderer::recordCommandBuffer(uint32_t frameIdx) {
  auto cmdBuffer = pCommandBuffers[frameIdx];
  auto frameBuffer = pFrameBuffers[frameIdx];

  cmdBuffer->beginRenderPass(RenderPassInfo{
      .pRenderPass = pRenderPass,
      .pFrameBuffer = frameBuffer,
      .renderArea = renderArea,
  });

  cmdBuffer->bindPipeline(PipelineInfo{
      .pPipeline = pPipeline,
  });

  cmdBuffer->setViewport();
  cmdBuffer->setScissor();

  cmdBuffer->bindMeshList(MeshListInfo{
    .pMeshList = pMeshes,
  });


  // 绑定描述符集
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 1, &descriptorSets[frameIdx], 0,
                          nullptr);

  if (!GLOBAL_CONTROL_RENDER_BLACK) {
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0,
                     0, 0);
  }

  // 渲染 ImGui
  std::lock_guard<std::mutex> lock(drawDataMutex);
  if (drawDataForRenderThread) {
    ImGui_ImplVulkan_RenderDrawData(drawDataForRenderThread, commandBuffer);
  }
}
} // namespace LX_graphics