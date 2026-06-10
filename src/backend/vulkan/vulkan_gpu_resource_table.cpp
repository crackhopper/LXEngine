#include "backend/vulkan/vulkan_gpu_resource_table.hpp"

#include <utility>

namespace LX_core {

GpuBufferHandle VulkanGpuResourceTable::createBuffer(
    const GpuBufferDesc &desc, std::span<const u8> initialData) {
  (void)desc;
  (void)initialData;
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
  return GpuImageHandle{m_nextId++};
}

GpuSamplerHandle
VulkanGpuResourceTable::createSampler(const GpuSamplerDesc &desc) {
  (void)desc;
  return GpuSamplerHandle{m_nextId++};
}

GpuDescriptorTableHandle VulkanGpuResourceTable::createDescriptorTable() {
  return GpuDescriptorTableHandle{m_nextId++};
}

GpuBindlessSlot VulkanGpuResourceTable::updateBindlessSlot(
    GpuDescriptorTableHandle table, GpuImageHandle image,
    GpuSamplerHandle sampler) {
  (void)table;
  (void)image;
  (void)sampler;
  return GpuBindlessSlot{0};
}

GpuIndirectDrawBufferHandle
VulkanGpuResourceTable::updateIndirectDrawBuffer(
    std::span<const u8> drawCommands) {
  (void)drawCommands;
  return GpuIndirectDrawBufferHandle{m_nextId++};
}

std::optional<GpuPipelineHandle>
VulkanGpuResourceTable::findPipeline(const GpuPipelineDesc &desc) const {
  (void)desc;
  return std::nullopt;
}

GpuPipelineHandle
VulkanGpuResourceTable::getOrCreatePipeline(const GpuPipelineDesc &desc) {
  (void)desc;
  return GpuPipelineHandle{m_nextId++};
}

void VulkanGpuResourceTable::importPipelineCache(std::span<const u8> bytes) {
  m_pipelineCache.assign(bytes.begin(), bytes.end());
}

std::vector<u8> VulkanGpuResourceTable::exportPipelineCache() const {
  return m_pipelineCache;
}

GpuProgress VulkanGpuResourceTable::queryProgress() const { return {}; }

} // namespace LX_core
