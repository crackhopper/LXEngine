#pragma once

#include "core/rhi/gpu_resource.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "pipelines/pipeline_cache.hpp"
#include "pipelines/pipeline.hpp"
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include <vulkan/vulkan.h>

namespace LX_core {
struct RenderingItem;
} // namespace LX_core

namespace LX_core::backend {

class VulkanDevice;
class VulkanCommandBufferManager;
class VulkanRenderPass;
class VulkanBuffer;
class VulkanTexture;
class VulkanShader;

using VulkanBufferUniquePtr = std::unique_ptr<VulkanBuffer>;
using VulkanTextureUniquePtr = std::unique_ptr<VulkanTexture>;

using VulkanAnyResource =
    std::variant<VulkanBufferUniquePtr, VulkanTextureUniquePtr>;

class VulkanResourceManager;
using VulkanResourceManagerUniquePtr = std::unique_ptr<VulkanResourceManager>;
class VulkanResourceManager {
  struct Token {};
  struct CachedGpuResource {
    std::shared_ptr<VulkanAnyResource> resource;
    ResourceCacheIdentity lastSeenFrame = 0;
  };

public:
  explicit VulkanResourceManager(Token token, VulkanDevice &device);
  ~VulkanResourceManager();

  static VulkanResourceManagerUniquePtr create(VulkanDevice &device) {
    auto p = std::make_unique<VulkanResourceManager>(Token{}, device);
    return p;
  }

  VulkanResourceManager(const VulkanResourceManager &) = delete;
  VulkanResourceManager &operator=(const VulkanResourceManager &) = delete;

  void syncResource(VulkanCommandBufferManager &cmdBufferManager,
                    const IGpuResourceSharedPtr &cpuRes);
  void collectGarbage();

  void initializeRenderPassAndPipeline(VkSurfaceFormatKHR surfaceFormat,
                                       VkFormat depthFormat);

  std::optional<std::reference_wrapper<VulkanBuffer>>
  getBuffer(ResourceCacheIdentity identity);
  std::optional<std::reference_wrapper<VulkanTexture>>
  getTexture(ResourceCacheIdentity identity);
  VulkanRenderPass &getRenderPass();

  /// Delegates to the embedded PipelineCache. Kept for backward compatibility
  /// with tests and the renderer hot path; prefers a preloaded cache.
  VulkanPipeline &getOrCreateRenderPipeline(const LX_core::RenderingItem &item);

  /// Bulk preload — intended to be called once per scene init from the
  /// VulkanRenderer after building a FrameGraph.
  void preloadPipelines(const std::vector<LX_core::PipelineBuildDesc> &infos);

  PipelineCache &getPipelineCache() { return *m_pipelineCache; }
  usize getCachedResourceCount() const { return m_gpuResources.size(); }

private:
  std::shared_ptr<VulkanAnyResource>
  createGpuResource(const IGpuResourceSharedPtr &cpuRes);
  void updateGpuResource(std::shared_ptr<VulkanAnyResource> &gpuRes,
                         const IGpuResourceSharedPtr &cpuRes,
                         VulkanCommandBufferManager &cmdBufferManager);

  VulkanDevice &m_device;
  std::unordered_map<ResourceCacheIdentity, CachedGpuResource> m_gpuResources;
  std::unordered_set<ResourceCacheIdentity> m_activeResourceIds;
  ResourceCacheIdentity m_frameSerial = 0;

  std::unique_ptr<VulkanRenderPass> m_renderPass;
  std::unique_ptr<PipelineCache> m_pipelineCache;
};

} // namespace LX_core::backend
