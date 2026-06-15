#pragma once

#include "core/frame_graph/render_input.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"

#include <memory>
#include <vector>

namespace LX_core {
class FrameGraph;
class CompiledFrameGraph;
} // namespace LX_core

namespace LX_core::backend {
class VulkanCommandBufferManager;
class VulkanDevice;
class VulkanResourceManager;
} // namespace LX_core::backend

namespace LX_core::backend::offline {

struct OfflineGraphExecutionResult {
  GpuResourceRef outputPixels;
};

[[nodiscard]] GpuResourceRef
resolveComputeReadbackResource(const RenderComputeInput &input,
                               const RenderInputDesc &desc);

class OfflineRenderGraphExecutor final {
public:
  OfflineRenderGraphExecutor(VulkanDevice &device,
                             VulkanCommandBufferManager &commandManager,
                             VulkanResourceManager &resourceManager);

  [[nodiscard]] OfflineGraphExecutionResult
  execute(const FrameGraph &graph, const CompiledFrameGraph &compiledGraph,
          const std::vector<std::vector<std::unique_ptr<RenderInput>>>
              &passInputs,
          const std::vector<std::vector<RenderInputDesc>> &passDescs);

private:
  VulkanDevice &m_device;
  VulkanCommandBufferManager &m_commandManager;
  VulkanResourceManager &m_resourceManager;
};

} // namespace LX_core::backend::offline
