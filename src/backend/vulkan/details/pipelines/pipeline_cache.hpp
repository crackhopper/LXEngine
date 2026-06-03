#pragma once

#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "compute_pipeline.hpp"
#include "graphics_pipeline.hpp"
#include "pipeline_ref.hpp"
#include <vulkan/vulkan.h>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace LX_core::backend {

class VulkanDevice;

/// 独立的 pipeline 缓存。通过 `PipelineKey` 索引 `VulkanGraphicsPipeline`，
/// 提供 find / getOrCreate / preload 三个入口。
/// `VulkanResourceManager` 委托本类，不再自己维护 pipeline map。
class PipelineCache {
public:
  explicit PipelineCache(VulkanDevice &device);

  /// 只查不建：miss 返回 nullopt，size 不变。
  std::optional<std::reference_wrapper<VulkanGraphicsPipeline>>
  find(const PipelineKey &key) const;

  /// Miss 则按 buildInfo 新建并缓存。
  /// Preload 阶段以外的 miss 会打印 warn 日志（含 toDebugString）。
  VulkanGraphicsPipeline &getOrCreate(const PipelineBuildDesc &info,
                                      VkRenderPass renderPass);

  VulkanComputePipeline &getOrCreateCompute(const PipelineBuildDesc &info);

  VulkanPipelineRef getOrCreatePipeline(const PipelineBuildDesc &info,
                                        VkRenderPass renderPass);

  /// 批量预构建：抑制 miss 警告，循环调用 getOrCreate。
  void preload(const std::vector<PipelineBuildDesc> &infos,
               VkRenderPass renderPass);

  usize size() const { return m_cache.size() + m_computeCache.size(); }

private:
  VulkanDevice &m_device;
  std::unordered_map<PipelineKey, VulkanGraphicsPipelineUniquePtr,
                     PipelineKey::Hash>
      m_cache;
  std::unordered_map<PipelineKey, VulkanComputePipelineUniquePtr,
                     PipelineKey::Hash>
      m_computeCache;
  bool m_suppressMissWarning = false;
};

} // namespace LX_core::backend
