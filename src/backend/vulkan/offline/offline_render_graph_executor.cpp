#include "offline_render_graph_executor.hpp"

#include "backend/vulkan/details/commands/command_buffer.hpp"
#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/frame_graph/frame_graph.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace LX_core::backend::offline {
namespace {

void syncDescriptorResources(VulkanResourceManager &resourceManager,
                             VulkanCommandBufferManager &commandManager,
                             const DescriptorResourceList &resources) {
  for (const DescriptorResourceRef &resource : resources) {
    if (resource.isTextureArray()) {
      for (const TextureSamplerRef &texture : resource.textures()) {
        if (texture.isValid()) {
          resourceManager.syncResource(commandManager,
                                       GpuResourceRef{texture.get()});
        }
      }
      continue;
    }
    if (resource.resource().isValid()) {
      resourceManager.syncResource(commandManager, resource.resource());
    }
  }
}

} // namespace

GpuResourceRef resolveComputeReadbackResource(const RenderComputeInput &input,
                                              const RenderInputDesc &desc) {
  if (!input.readbackResource.has_value()) {
    throw std::runtime_error(
        "offline compute input missing readback resource binding");
  }
  const StringID bindingName = *input.readbackResource;
  const auto it = std::find_if(
      desc.bindingPlan.descriptors.begin(), desc.bindingPlan.descriptors.end(),
      [bindingName](const DescriptorResourceRef &resource) {
        return resource.getBindingName() == bindingName;
      });
  if (it == desc.bindingPlan.descriptors.end()) {
    throw std::runtime_error("offline compute readback resource binding '" +
                             GlobalStringTable::get().toDebugString(bindingName) +
                             "' missing");
  }
  if (!it->isResource()) {
    throw std::runtime_error("offline compute readback resource binding '" +
                             GlobalStringTable::get().toDebugString(bindingName) +
                             "' is not a resource");
  }
  const GpuResourceRef resource = it->resource();
  if (!resource.isValid()) {
    throw std::runtime_error("offline compute readback resource binding '" +
                             GlobalStringTable::get().toDebugString(bindingName) +
                             "' is invalid");
  }
  return resource;
}

OfflineRenderGraphExecutor::OfflineRenderGraphExecutor(
    VulkanDevice &device, VulkanCommandBufferManager &commandManager,
    VulkanResourceManager &resourceManager)
    : m_device(device), m_commandManager(commandManager),
      m_resourceManager(resourceManager) {}

OfflineGraphExecutionResult
OfflineRenderGraphExecutor::execute(const FrameGraph &graph,
                                    const CompiledFrameGraph &compiledGraph,
                                    const std::vector<std::vector<std::unique_ptr<RenderInput>>>
                                        &passInputs,
                                    const std::vector<std::vector<RenderInputDesc>>
                                        &passDescs) {
  const auto &graphPasses = graph.getPasses();
  const auto &compiledPasses = compiledGraph.getPasses();
  if (graphPasses.empty()) {
    throw std::runtime_error("offline render graph has no passes");
  }
  if (graphPasses.size() != compiledPasses.size()) {
    throw std::runtime_error("offline render graph pass count mismatch");
  }
  if (graphPasses.size() != passInputs.size() ||
      graphPasses.size() != passDescs.size()) {
    throw std::runtime_error("offline render graph input count mismatch");
  }

  OfflineGraphExecutionResult result;
  for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
    const usize sourcePassIndex = compiledPasses[passIndex].sourcePassIndex;
    if (sourcePassIndex >= graphPasses.size() ||
        sourcePassIndex >= passInputs.size() || sourcePassIndex >= passDescs.size()) {
      throw std::runtime_error("offline render graph source pass mismatch");
    }
    const FramePass &pass = graphPasses[sourcePassIndex];
    const auto &inputs = passInputs[sourcePassIndex];
    const auto &descs = passDescs[sourcePassIndex];
    if (pass.name != compiledPasses[passIndex].name) {
      throw std::runtime_error("offline render graph pass order mismatch");
    }
    for (const RenderInputDesc &desc : descs) {
      if (desc.accepted()) {
        syncDescriptorResources(m_resourceManager, m_commandManager,
                                desc.bindingPlan.descriptors);
      }
    }

    if (descs.empty()) {
      continue;
    }

    auto cmd = m_commandManager.beginSingleTimeCommands();
    for (const RenderInputDesc &desc : descs) {
      if (!desc.accepted()) {
        continue;
      }
      if (desc.inputIndex >= inputs.size() || !inputs[desc.inputIndex]) {
        throw std::runtime_error("offline render graph desc input missing");
      }
      const RenderInput &input = *inputs[desc.inputIndex];
      if (input.kind() != RenderInputKind::Compute) {
        throw std::runtime_error(
            "offline render graph only supports compute inputs");
      }

      VulkanPipelineRef pipeline = m_resourceManager.getOrCreatePipeline(desc);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(m_resourceManager, pipeline, input, desc);
      cmd->executeRenderInput(input, desc);

      const auto &computeInput = static_cast<const RenderComputeInput &>(input);
      GpuResourceRef outputPixels =
          resolveComputeReadbackResource(computeInput, desc);
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
