#include "backend/vulkan/vulkan_gpu_resource_table.hpp"

#include "core/scene/scene_resource_table_upload_view.hpp"

#include <utility>
#include <vector>

namespace LX_core {
namespace {

template <typename T>
[[nodiscard]] std::span<const u8> bytesForSpan(std::span<const T> values) {
  return std::span<const u8>{
      reinterpret_cast<const u8 *>(values.data()),
      static_cast<usize>(sizeof(T) * values.size())};
}

[[nodiscard]] SceneBindlessStagedBuffer
createSpanBuffer(VulkanGpuResourceTable &table, std::span<const u8> bytes) {
  return SceneBindlessStagedBuffer{
      .buffer = table.createBuffer(
          GpuBufferDesc{.byteSize = static_cast<u64>(bytes.size())}, bytes),
      .byteSize = static_cast<u64>(bytes.size()),
  };
}

[[nodiscard]] std::vector<u8> sourceMaterialBytes(
    const SceneResourceTableUploadView &view,
    const SceneSourceLocalMaterialStorageView &storage,
    std::vector<std::string> &diagnostics) {
  std::vector<u8> bytes;
  if (storage.recordOffset + storage.recordCount >
      view.sourceMaterialRecords.size()) {
    diagnostics.push_back("invalid source material storage range");
    return bytes;
  }

  for (u32 i = 0; i < storage.recordCount; ++i) {
    const SourceLocalMaterialRecord &record =
        view.sourceMaterialRecords[storage.recordOffset + i];
    bytes.insert(bytes.end(), record.bytes.begin(), record.bytes.end());
  }
  return bytes;
}

void validateMaterialRefs(const SceneResourceTableUploadView &view,
                          std::vector<std::string> &diagnostics) {
  for (u32 i = 0; i < view.materialRefs.size(); ++i) {
    const SceneGpuMaterialRefRecord &ref = view.materialRefs[i];
    if (ref.sourceStorageIndex >= view.sourceMaterialStorages.size()) {
      diagnostics.push_back("material ref has invalid sourceStorageIndex");
      continue;
    }
    const SceneSourceLocalMaterialStorageView &storage =
        view.sourceMaterialStorages[ref.sourceStorageIndex];
    if (ref.sourceLocalMaterialIndex >= storage.recordCount) {
      diagnostics.push_back(
          "material ref has invalid sourceLocalMaterialIndex");
    }
  }

  for (const SceneGpuDrawRecord &draw : view.draws) {
    if (draw.materialRefIndex != u32_max &&
        draw.materialRefIndex >= view.materialRefs.size()) {
      diagnostics.push_back("draw has invalid materialRefIndex");
    }
  }
}

} // namespace

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

SceneBindlessUploadReport VulkanGpuResourceTable::uploadSceneBindlessTables(
    const SceneResourceTableUploadView &view) {
  SceneBindlessUploadReport report;
  validateMaterialRefs(view, report.diagnostics);

  const GpuDescriptorTableHandle descriptors = createDescriptorTable();
  report.textureSlots.reserve(view.textures.size());
  for (u32 i = 0; i < view.textures.size(); ++i) {
    const CombinedTextureSampler &texture = view.textures[i].get();
    const TextureDesc &desc = texture.texture()->desc();
    const GpuImageHandle image = createImage(
        GpuImageDesc{.width = desc.width,
                     .height = desc.height,
                     .mipLevels = desc.mipLevels},
        std::span<const u8>{
            static_cast<const u8 *>(texture.getRawData()),
            static_cast<usize>(texture.getByteSize())});
    const GpuSamplerHandle sampler =
        createSampler(GpuSamplerDesc{.linear = true});
    const GpuBindlessSlot slot =
        updateBindlessSlot(descriptors, image, sampler);
    report.textureSlots.push_back(SceneBindlessTextureSlot{
        .textureTableIndex = i,
        .image = image,
        .sampler = sampler,
        .slot = slot,
    });
  }

  report.materialStorageBuffers.reserve(view.sourceMaterialStorages.size());
  for (u32 i = 0; i < view.sourceMaterialStorages.size(); ++i) {
    const SceneSourceLocalMaterialStorageView &storage =
        view.sourceMaterialStorages[i];
    const std::vector<u8> bytes =
        sourceMaterialBytes(view, storage, report.diagnostics);
    const SceneBindlessStagedBuffer stagedBuffer = createSpanBuffer(
        *this, std::span<const u8>{bytes.data(), bytes.size()});
    report.materialStorageBuffers.push_back(SceneBindlessMaterialStorage{
        .sourceStorageIndex = i,
        .sourceSignature = storage.sourceSignature,
        .storageAbiHash = storage.storageAbiHash,
        .recordOffset = storage.recordOffset,
        .recordCount = storage.recordCount,
        .buffer = stagedBuffer.buffer,
        .byteSize = stagedBuffer.byteSize,
    });
  }

  report.objectBuffer = createSpanBuffer(*this, bytesForSpan(view.objects));
  report.drawBuffer = createSpanBuffer(*this, bytesForSpan(view.draws));
  report.meshBuffer = createSpanBuffer(*this, bytesForSpan(view.meshes));
  report.positionBuffer =
      createSpanBuffer(*this, bytesForSpan(view.positions));
  report.indexBuffer = createSpanBuffer(*this, bytesForSpan(view.indices));
  report.primitiveBuffer =
      createSpanBuffer(*this, bytesForSpan(view.primitives));
  report.attributeStreamBuffer =
      createSpanBuffer(*this, bytesForSpan(view.attributeStreams));
  report.attributeValueBuffer =
      createSpanBuffer(*this, bytesForSpan(view.attributeValues));
  return report;
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
