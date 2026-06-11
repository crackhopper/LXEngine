#include "backend/vulkan/vulkan_gpu_resource_table.hpp"

#include <utility>

namespace LX_core {

GpuBufferHandle VulkanGpuResourceTable::createBuffer(
    const GpuBufferDesc &desc, std::span<const u8> initialData) {
  (void)desc;
  (void)initialData;
  ++m_totalTasks;
  ++m_completedTasks;
  return GpuBufferHandle{m_nextId++};
}

void VulkanGpuResourceTable::updateBuffer(GpuBufferHandle handle, u64 offset,
                                          std::span<const u8> data) {
  (void)handle;
  (void)offset;
  (void)data;
}

GpuImageHandle VulkanGpuResourceTable::createImage(
    const GpuImageDesc &desc, std::span<const u8> initialData) {
  (void)desc;
  (void)initialData;
  ++m_totalTasks;
  ++m_completedTasks;
  return GpuImageHandle{m_nextId++};
}

GpuSamplerHandle
VulkanGpuResourceTable::createSampler(const GpuSamplerDesc &desc) {
  (void)desc;
  ++m_totalTasks;
  ++m_completedTasks;
  return GpuSamplerHandle{m_nextId++};
}

GpuDescriptorTableHandle VulkanGpuResourceTable::createDescriptorTable() {
  return GpuDescriptorTableHandle{m_nextId++};
}

GpuBindlessSlot VulkanGpuResourceTable::updateBindlessSlot(
    GpuDescriptorTableHandle table, GpuImageHandle image,
    GpuSamplerHandle sampler) {
  const VulkanBindlessKey key{table.id, image.id, sampler.id};
  if (const auto it = m_bindlessSlots.find(key); it != m_bindlessSlots.end()) {
    return it->second;
  }

  const GpuBindlessSlot slot{static_cast<u32>(m_bindlessSlots.size())};
  m_bindlessSlots.emplace(key, slot);
  ++m_totalTasks;
  ++m_completedTasks;
  return slot;
}

GpuIndirectDrawBufferHandle
VulkanGpuResourceTable::updateIndirectDrawBuffer(
    std::span<const u8> drawCommands) {
  (void)drawCommands;
  return GpuIndirectDrawBufferHandle{m_nextId++};
}

std::optional<GpuPipelineHandle>
VulkanGpuResourceTable::findPipeline(const GpuPipelineDesc &desc) const {
  if (const auto it = m_pipelines.find(desc.key); it != m_pipelines.end()) {
    return it->second;
  }
  return std::nullopt;
}

GpuPipelineHandle
VulkanGpuResourceTable::getOrCreatePipeline(const GpuPipelineDesc &desc) {
  if (const auto existing = findPipeline(desc); existing.has_value()) {
    return *existing;
  }

  const GpuPipelineHandle handle{m_nextId++};
  m_pipelines.emplace(desc.key, handle);
  ++m_totalTasks;
  ++m_completedTasks;
  return handle;
}

void VulkanGpuResourceTable::importPipelineCache(std::span<const u8> bytes) {
  m_pipelineCache.assign(bytes.begin(), bytes.end());
}

std::vector<u8> VulkanGpuResourceTable::exportPipelineCache() const {
  return m_pipelineCache;
}

GpuProgress VulkanGpuResourceTable::queryProgress() const {
  GpuProgress progress;
  progress.completedTasks = m_completedTasks;
  progress.totalTasks = m_totalTasks;
  progress.currentTask =
      (m_completedTasks == m_totalTasks) ? "ready" : "uploading";
  return progress;
}

} // namespace LX_core
