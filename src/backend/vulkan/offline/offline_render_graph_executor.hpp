#pragma once

#include "core/rhi/gpu_resource.hpp"

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
  IGpuResourceSharedPtr outputPixels;
};

class OfflineRenderGraphExecutor final {
public:
  OfflineRenderGraphExecutor(VulkanDevice &device,
                             VulkanCommandBufferManager &commandManager,
                             VulkanResourceManager &resourceManager);

  [[nodiscard]] OfflineGraphExecutionResult
  execute(const FrameGraph &graph, const CompiledFrameGraph &compiledGraph);

private:
  VulkanDevice &m_device;
  VulkanCommandBufferManager &m_commandManager;
  VulkanResourceManager &m_resourceManager;
};

} // namespace LX_core::backend::offline
