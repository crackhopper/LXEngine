#pragma once

#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "pipeline.hpp"
#include <vulkan/vulkan.h>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace LX_core::backend {

class VulkanDevice;

/// 独立的 pipeline 缓存。通过 `PipelineKey` 索引 `VulkanPipeline`，
/// 提供 find / getOrCreate / preload 三个入口。
/// `VulkanResourceManager` 委托本类，不再自己维护 pipeline map。
class PipelineCache {
public:
  explicit PipelineCache(VulkanDevice &device);

  /// 只查不建：miss 返回 nullopt，size 不变。
  std::optional<std::reference_wrapper<VulkanPipeline>>
  find(const PipelineKey &key) const;

  /// Miss 则按 buildInfo 新建并缓存。
  /// Preload 阶段以外的 miss 会打印 warn 日志（含 toDebugString）。
  VulkanPipeline &getOrCreate(const PipelineBuildDesc &info,
                              VkRenderPass renderPass);

  /// 批量预构建：抑制 miss 警告，循环调用 getOrCreate。
  void preload(const std::vector<PipelineBuildDesc> &infos,
               VkRenderPass renderPass);

  usize size() const { return m_cache.size(); }

private:
  VulkanDevice &m_device;
  std::unordered_map<PipelineKey, VulkanPipelineUniquePtr, PipelineKey::Hash> m_cache;
  bool m_suppressMissWarning = false;
};

} // namespace LX_core::backend
