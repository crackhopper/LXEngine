#pragma once

#include "core/platform/types.hpp"

#include <span>
#include <optional>
#include <string>
#include <vector>

namespace LX_core {

struct GpuBufferHandle final {
  u64 id = 0;
};
struct GpuImageHandle final {
  u64 id = 0;
};
struct GpuSamplerHandle final {
  u64 id = 0;
};
struct GpuDescriptorTableHandle final {
  u64 id = 0;
};
struct GpuBindlessSlot final {
  u32 index = u32_max;
};
struct GpuIndirectDrawBufferHandle final {
  u64 id = 0;
};
struct GpuPipelineHandle final {
  u64 id = 0;
};

struct GpuProgress final {
  u32 completedTasks = 0;
  u32 totalTasks = 0;
  std::string currentTask;
  std::vector<std::string> diagnostics;
};

struct GpuBufferDesc final {
  u64 byteSize = 0;
};

struct GpuImageDesc final {
  u32 width = 0;
  u32 height = 0;
  u32 mipLevels = 1;
};

struct GpuSamplerDesc final {
  bool linear = true;
};

struct GpuPipelineDesc final {
  std::string key;
};

class IGpuResourceTable {
public:
  virtual ~IGpuResourceTable() = default;

  [[nodiscard]] virtual GpuBufferHandle createBuffer(
      const GpuBufferDesc &desc, std::span<const u8> initialData) = 0;
  virtual void updateBuffer(GpuBufferHandle handle, u64 offset,
                            std::span<const u8> data) = 0;
  [[nodiscard]] virtual GpuImageHandle createImage(
      const GpuImageDesc &desc, std::span<const u8> initialData) = 0;
  [[nodiscard]] virtual GpuSamplerHandle
  createSampler(const GpuSamplerDesc &desc) = 0;
  [[nodiscard]] virtual GpuDescriptorTableHandle createDescriptorTable() = 0;
  [[nodiscard]] virtual GpuBindlessSlot
  updateBindlessSlot(GpuDescriptorTableHandle table, GpuImageHandle image,
                     GpuSamplerHandle sampler) = 0;
  [[nodiscard]] virtual GpuIndirectDrawBufferHandle
  updateIndirectDrawBuffer(std::span<const u8> drawCommands) = 0;
  [[nodiscard]] virtual std::optional<GpuPipelineHandle>
  findPipeline(const GpuPipelineDesc &desc) const = 0;
  [[nodiscard]] virtual GpuPipelineHandle
  getOrCreatePipeline(const GpuPipelineDesc &desc) = 0;
  virtual void importPipelineCache(std::span<const u8> bytes) = 0;
  [[nodiscard]] virtual std::vector<u8> exportPipelineCache() const = 0;
  [[nodiscard]] virtual GpuProgress queryProgress() const = 0;
};

} // namespace LX_core
