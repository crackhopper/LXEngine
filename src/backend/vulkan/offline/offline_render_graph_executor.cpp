#include "offline_render_graph_executor.hpp"

#include "backend/vulkan/details/commands/command_buffer.hpp"
#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_upload_plan.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace LX_core::backend::offline {
namespace {

[[nodiscard]] GpuResourceRef
findPipelineResource(const RenderWorkItem &item, StringID bindingName) {
  const auto it = std::find_if(
      item.descriptorResources.begin(), item.descriptorResources.end(),
      [bindingName](const DescriptorResourceRef &resource) {
        return resource.getBindingName() == bindingName;
      });
  if (it == item.descriptorResources.end() || !it->isResource()) {
    return {};
  }
  return it->resource();
}

} // namespace

OfflineRenderGraphExecutor::OfflineRenderGraphExecutor(
    VulkanDevice &device, VulkanCommandBufferManager &commandManager,
    VulkanResourceManager &resourceManager)
    : m_device(device), m_commandManager(commandManager),
      m_resourceManager(resourceManager) {}

OfflineGraphExecutionResult
OfflineRenderGraphExecutor::execute(const FrameGraph &graph,
                                    const CompiledFrameGraph &compiledGraph) {
  const auto &graphPasses = graph.getPasses();
  const auto &compiledPasses = compiledGraph.getPasses();
  if (graphPasses.empty()) {
    throw std::runtime_error("offline render graph has no passes");
  }
  if (graphPasses.size() != compiledPasses.size()) {
    throw std::runtime_error("offline render graph pass count mismatch");
  }

  OfflineGraphExecutionResult result;
  for (usize passIndex = 0; passIndex < graphPasses.size(); ++passIndex) {
    const FramePass &pass = graphPasses[passIndex];
    if (pass.name != compiledPasses[passIndex].name) {
      throw std::runtime_error("offline render graph pass order mismatch");
    }
    const RenderUploadPlan uploadPlan = buildRenderUploadPlan(pass.queue);
    for (const auto &resource : uploadPlan.resources) {
      m_resourceManager.syncResource(m_commandManager, resource);
    }

    if (pass.queue.getItems().empty()) {
      continue;
    }

    auto cmd = m_commandManager.beginSingleTimeCommands();
    for (const RenderWorkItem &item : pass.queue.getItems()) {
      if (item.domain != RenderDomain::Offline ||
          item.kind != RenderWorkKind::ComputeDispatch) {
        throw std::runtime_error(
            "offline render graph only supports offline compute work");
      }

      VulkanPipelineRef pipeline = m_resourceManager.getOrCreatePipeline(item);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(m_resourceManager, pipeline, item);
      cmd->executeWorkItem(item);

      GpuResourceRef outputPixels =
          findPipelineResource(item, StringID("OutputPixels"));
      if (outputPixels.isValid()) {
        result.outputPixels = std::move(outputPixels);
      }
    }

    m_commandManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
  }

  if (!result.outputPixels.isValid()) {
    throw std::runtime_error("offline output storage buffer missing");
  }
  auto cmd = m_commandManager.beginSingleTimeCommands();
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cmd->getHandle(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                       0, nullptr);
  m_commandManager.endSingleTimeCommands(std::move(cmd),
                                         m_device.getGraphicsQueue());
  return result;
}

} // namespace LX_core::backend::offline
