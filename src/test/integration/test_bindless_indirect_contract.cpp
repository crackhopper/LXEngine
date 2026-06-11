#include "backend/vulkan/vulkan_gpu_resource_table.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/scene_gpu_records.hpp"

#include <iostream>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct TestGpuResource final : public IGpuResource {
  TestGpuResource(ResourceType type, StringID bindingName, u32 byteSize)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type = ResourceType::None;
  StringID bindingName;
  std::vector<u8> bytes;
};

RenderWorkItem makeDrawItem(const IGpuResource &vertex,
                            const IGpuResource &index,
                            const IGpuResource &camera, PipelineKey key,
                            u32 firstIndex) {
  RenderWorkItem item;
  item.kind = RenderWorkKind::RasterDraw;
  item.pass = StringID("Forward");
  RenderTargetDesc target;
  target.role = RenderTargetRole::Swapchain;
  target.colorFormat = ImageFormat::BGRA8;
  target.depthFormat = ImageFormat::D32Float;
  item.target = target;
  item.pipelineKey = key;
  item.raster.vertexBuffer = GpuResourceRef{vertex};
  item.raster.indexBuffer = GpuResourceRef{index};
  item.raster.indexCount = 6;
  item.raster.instanceCount = 1;
  item.raster.firstIndex = firstIndex;
  item.descriptorResources.emplace_back(camera);
  return item;
}

void testSharedTextureIdentityProducesOneBindlessSlot() {
  VulkanGpuResourceTable table;
  const GpuImageHandle imageA{42};
  const GpuImageHandle imageB{42};
  const GpuSamplerHandle sampler{7};
  const GpuDescriptorTableHandle descriptors = table.createDescriptorTable();

  const GpuBindlessSlot first =
      table.updateBindlessSlot(descriptors, imageA, sampler);
  const GpuBindlessSlot second =
      table.updateBindlessSlot(descriptors, imageB, sampler);

  EXPECT(first.index == second.index,
         "same image/sampler identity should reuse bindless slot");

  SceneGpuMaterialRecord materialA;
  SceneGpuMaterialRecord materialB;
  materialA.baseColorTexture = first.index;
  materialB.baseColorTexture = second.index;
  EXPECT(materialA.baseColorTexture == materialB.baseColorTexture,
         "two material records should point at the same texture slot");
}

void testDifferentTextureIdentityProducesDifferentBindlessSlots() {
  VulkanGpuResourceTable table;
  const GpuDescriptorTableHandle descriptors = table.createDescriptorTable();

  const GpuBindlessSlot first =
      table.updateBindlessSlot(descriptors, GpuImageHandle{42},
                               GpuSamplerHandle{7});
  const GpuBindlessSlot second =
      table.updateBindlessSlot(descriptors, GpuImageHandle{43},
                               GpuSamplerHandle{7});

  EXPECT(first.index != second.index,
         "different image identity should allocate a different bindless slot");
}

void testPipelineCacheFindAndCreateAreObservable() {
  VulkanGpuResourceTable table;
  const GpuPipelineDesc desc{.key = "forward.bindless.pipeline"};

  EXPECT(!table.findPipeline(desc).has_value(),
         "pipeline find should miss before getOrCreate");
  const GpuPipelineHandle created = table.getOrCreatePipeline(desc);
  const auto found = table.findPipeline(desc);

  EXPECT(found.has_value(), "pipeline find should hit after getOrCreate");
  EXPECT(found->id == created.id, "pipeline cache should return same handle");
}

void testProgressTracksResourceWork() {
  VulkanGpuResourceTable table;
  const u8 bytes[4] = {1, 2, 3, 4};

  (void)table.createBuffer(GpuBufferDesc{.byteSize = 4},
                           std::span<const u8>(bytes));
  (void)table.createImage(GpuImageDesc{.width = 1, .height = 1, .mipLevels = 1},
                          std::span<const u8>(bytes));

  const GpuProgress progress = table.queryProgress();
  EXPECT(progress.totalTasks >= 2,
         "resource table progress should count created resources");
  EXPECT(progress.completedTasks == progress.totalTasks,
         "synchronous shell uploads should report completed tasks");
}

void testIndirectDrawBufferHandleIsOpaque() {
  VulkanGpuResourceTable table;
  const u8 commandBytes[16] = {};
  const auto handle =
      table.updateIndirectDrawBuffer(std::span<const u8>(commandBytes));
  EXPECT(handle.id != 0, "indirect draw buffer handle should be valid");
}

void testQueueCompilesStableIndirectBatches() {
  TestGpuResource vertex(ResourceType::VertexBuffer, StringID{}, 128);
  TestGpuResource index(ResourceType::IndexBuffer, StringID{}, 96);
  TestGpuResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"),
                         64);
  const PipelineKey key =
      PipelineKey::build(StringID("mesh"), StringID("mat"),
                         StringID("swapchain.bgra8.d32"));

  RenderWorkQueue queue;
  queue.addItem(makeDrawItem(vertex, index, camera, key, 0));
  queue.addItem(makeDrawItem(vertex, index, camera, key, 6));

  const auto batches = queue.compileIndirectBatches();
  EXPECT(batches.size() == 1,
         "compatible default draw items should compile into one batch");
  EXPECT(batches.front().commands.size() == 2,
         "batch should preserve both indirect commands");
  EXPECT(batches.front().commands[0].indexCount == 6,
         "indexCount should be copied into indirect command");
  EXPECT(batches.front().commands[1].firstIndex == 6,
         "firstIndex should be copied into indirect command");
  EXPECT(batches.front().sourceItemIndices[0] == 0 &&
             batches.front().sourceItemIndices[1] == 1,
         "batch should preserve source item order for diagnostics");
}

void testDescriptorChangeSplitsIndirectBatches() {
  TestGpuResource vertex(ResourceType::VertexBuffer, StringID{}, 128);
  TestGpuResource index(ResourceType::IndexBuffer, StringID{}, 96);
  TestGpuResource cameraA(ResourceType::UniformBuffer, StringID("CameraUBO"),
                          64);
  TestGpuResource cameraB(ResourceType::UniformBuffer, StringID("CameraUBO"),
                          64);
  const PipelineKey key =
      PipelineKey::build(StringID("mesh"), StringID("mat"),
                         StringID("swapchain.bgra8.d32"));

  RenderWorkQueue queue;
  queue.addItem(makeDrawItem(vertex, index, cameraA, key, 0));
  queue.addItem(makeDrawItem(vertex, index, cameraB, key, 6));

  const auto batches = queue.compileIndirectBatches();
  EXPECT(batches.size() == 2,
         "descriptor identity change must split indirect batches");
}

} // namespace

int main() {
  testSharedTextureIdentityProducesOneBindlessSlot();
  testDifferentTextureIdentityProducesDifferentBindlessSlots();
  testPipelineCacheFindAndCreateAreObservable();
  testProgressTracksResourceWork();
  testIndirectDrawBufferHandleIsOpaque();
  testQueueCompilesStableIndirectBatches();
  testDescriptorChangeSplitsIndirectBatches();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless/indirect checks failed\n";
    return 1;
  }
  return 0;
}
