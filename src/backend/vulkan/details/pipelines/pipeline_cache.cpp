#include "pipeline_cache.hpp"
#include "core/utils/string_table.hpp"
#include "../device.hpp"
#include "graphics_shader_program.hpp"

#include <iostream>

namespace LX_core::backend {

PipelineCache::PipelineCache(VulkanDevice &device) : m_device(device) {}

std::optional<std::reference_wrapper<VulkanPipeline>>
PipelineCache::find(const PipelineKey &key) const {
  auto it = m_cache.find(key);
  if (it == m_cache.end())
    return std::nullopt;
  return std::ref(*it->second);
}

VulkanPipeline &PipelineCache::getOrCreate(const PipelineBuildDesc &info,
                                           VkRenderPass renderPass) {
  auto it = m_cache.find(info.key);
  if (it != m_cache.end())
    return *it->second;

  if (!m_suppressMissWarning) {
    std::cerr << "[PipelineCache] miss: "
              << LX_core::GlobalStringTable::get().toDebugString(info.key.id)
              << "\n";
  }

  auto pipeline =
      VulkanShaderGraphicsPipeline::create(m_device, info, renderPass, {});
  VulkanPipeline *raw = pipeline.get();
  m_cache.emplace(info.key, std::move(pipeline));
  return *raw;
}

void PipelineCache::preload(const std::vector<PipelineBuildDesc> &infos,
                            VkRenderPass renderPass) {
  m_suppressMissWarning = true;
  for (const auto &info : infos) {
    (void)getOrCreate(info, renderPass);
  }
  m_suppressMissWarning = false;
}

} // namespace LX_core::backend
