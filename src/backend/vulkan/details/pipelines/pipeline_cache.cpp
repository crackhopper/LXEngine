#include "pipeline_cache.hpp"
#include "core/utils/string_table.hpp"
#include "../device.hpp"
#include "graphics_shader_program.hpp"

#include <iostream>
#include <stdexcept>

namespace LX_core::backend {

PipelineCache::PipelineCache(VulkanDevice &device) : m_device(device) {}

std::optional<std::reference_wrapper<VulkanGraphicsPipeline>>
PipelineCache::find(const PipelineKey &key) const {
  auto it = m_cache.find(key);
  if (it == m_cache.end())
    return std::nullopt;
  return std::ref(*it->second);
}

VulkanGraphicsPipeline &
PipelineCache::getOrCreate(const PipelineBuildDesc &info,
                           VkRenderPass renderPass) {
  if (info.type != PipelineBuildType::Graphics) {
    throw std::runtime_error("graphics pipeline cache requires graphics desc");
  }
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
  VulkanGraphicsPipeline *raw = pipeline.get();
  m_cache.emplace(info.key, std::move(pipeline));
  return *raw;
}

VulkanComputePipeline &
PipelineCache::getOrCreateCompute(const PipelineBuildDesc &info) {
  if (info.type != PipelineBuildType::Compute) {
    throw std::runtime_error("compute pipeline cache requires compute desc");
  }
  auto it = m_computeCache.find(info.key);
  if (it != m_computeCache.end()) {
    return *it->second;
  }

  if (!m_suppressMissWarning) {
    std::cerr << "[PipelineCache] compute miss: "
              << LX_core::GlobalStringTable::get().toDebugString(info.key.id)
              << "\n";
  }

  auto pipeline = VulkanComputePipeline::create(m_device, info);
  VulkanComputePipeline *raw = pipeline.get();
  m_computeCache.emplace(info.key, std::move(pipeline));
  return *raw;
}

VulkanPipelineRef
PipelineCache::getOrCreatePipeline(const PipelineBuildDesc &info,
                                   VkRenderPass renderPass) {
  switch (info.type) {
  case PipelineBuildType::Graphics:
    return std::ref(getOrCreate(info, renderPass));
  case PipelineBuildType::Compute:
    return std::ref(getOrCreateCompute(info));
  case PipelineBuildType::RayTracing:
    throw std::runtime_error("ray tracing pipeline cache is not implemented");
  }
  throw std::runtime_error("unknown pipeline build type");
}

void PipelineCache::preload(const std::vector<PipelineBuildDesc> &infos,
                            VkRenderPass renderPass) {
  m_suppressMissWarning = true;
  for (const auto &info : infos) {
    (void)getOrCreatePipeline(info, renderPass);
  }
  m_suppressMissWarning = false;
}

} // namespace LX_core::backend
