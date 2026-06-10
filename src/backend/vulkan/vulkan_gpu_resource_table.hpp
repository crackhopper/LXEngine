#pragma once

#include "core/rhi/gpu_resource_table.hpp"

namespace LX_core {

class VulkanGpuResourceTable final : public IGpuResourceTable {
public:
  [[nodiscard]] GpuBufferHandle createBuffer(
      const GpuBufferDesc &desc, std::span<const u8> initialData) override;
  void updateBuffer(GpuBufferHandle handle, u64 offset,
                    std::span<const u8> data) override;
  [[nodiscard]] GpuImageHandle createImage(
      const GpuImageDesc &desc, std::span<const u8> initialData) override;
  [[nodiscard]] GpuSamplerHandle
  createSampler(const GpuSamplerDesc &desc) override;
  [[nodiscard]] GpuDescriptorTableHandle createDescriptorTable() override;
  [[nodiscard]] GpuBindlessSlot
  updateBindlessSlot(GpuDescriptorTableHandle table, GpuImageHandle image,
                     GpuSamplerHandle sampler) override;
  [[nodiscard]] GpuIndirectDrawBufferHandle
  updateIndirectDrawBuffer(std::span<const u8> drawCommands) override;
  [[nodiscard]] std::optional<GpuPipelineHandle>
  findPipeline(const GpuPipelineDesc &desc) const override;
  [[nodiscard]] GpuPipelineHandle
  getOrCreatePipeline(const GpuPipelineDesc &desc) override;
  void importPipelineCache(std::span<const u8> bytes) override;
  [[nodiscard]] std::vector<u8> exportPipelineCache() const override;
  [[nodiscard]] GpuProgress queryProgress() const override;

private:
  u64 m_nextId = 1;
  std::vector<u8> m_pipelineCache;
};

} // namespace LX_core
