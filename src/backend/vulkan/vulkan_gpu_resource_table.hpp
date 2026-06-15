#pragma once

#include "core/rhi/gpu_resource_table.hpp"

#include <string>
#include <unordered_map>

namespace LX_core {

struct VulkanBindlessKey final {
  u64 table = 0;
  u64 image = 0;
  u64 sampler = 0;

  friend bool operator==(const VulkanBindlessKey &lhs,
                         const VulkanBindlessKey &rhs) = default;
};

struct VulkanBindlessKeyHash final {
  usize operator()(const VulkanBindlessKey &key) const noexcept {
    return static_cast<usize>((key.table * 1315423911ull) ^
                              (key.image * 2654435761ull) ^
                              (key.sampler * 97531ull));
  }
};

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
  [[nodiscard]] SceneBindlessUploadReport
  uploadSceneBindlessTables(const SceneResourceTableUploadView &view) override;
  [[nodiscard]] GpuProgress queryProgress() const override;

private:
  u64 m_nextId = 1;
  std::vector<u8> m_pipelineCache;
  std::unordered_map<VulkanBindlessKey, GpuBindlessSlot,
                     VulkanBindlessKeyHash>
      m_bindlessSlots;
  std::unordered_map<std::string, GpuPipelineHandle> m_pipelines;
  u32 m_completedTasks = 0;
  u32 m_totalTasks = 0;
};

} // namespace LX_core
