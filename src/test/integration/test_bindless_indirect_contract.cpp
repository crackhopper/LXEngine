#include "backend/vulkan/vulkan_gpu_resource_table.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_table.hpp"

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

struct SceneVertex final {
  Vec3f position;

  static VertexLayout getLayout() {
    return VertexLayout(
        std::vector<VertexLayoutItem>{
            VertexLayoutItem{"position", 0, DataType::Float3,
                             sizeof(Vec3f), 0}},
        sizeof(SceneVertex));
  }
};

MeshBufferUniquePtr makeTriangleMesh() {
  auto vertices = std::vector<SceneVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<SceneVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
             vb, ib,
             BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

MaterialInstanceUniquePtr makeSourceMaterial() {
  MaterialContractReflection contract;
  contract.sourceUri = ResourceUri("memory://materials/matte.contract.glsl");
  contract.declaredType = "matte";
  contract.reflectionHash = "matte-reflect-v1";
  contract.storageAbiHash = "matte-storage-v1";
  contract.accessorAbiHash = "material-surface-v1";
  contract.parameters.push_back(MaterialContractParameter{
      "Kd", true, {MaterialContractParameterKind::Rgb}});
  contract.storageFields.push_back(MaterialContractStorageField{
      .name = "baseColor",
      .type = MaterialContractStorageFieldType::Vec4,
      .inputKind = MaterialContractStorageInputKind::ParameterValue,
      .parameterName = "Kd",
      .defaultValue = Vec4f{1.0f, 1.0f, 1.0f, 1.0f},
  });

  auto material =
      MaterialInstance::createUnique(MaterialTemplate::create("matte"));
  material->setBsdfType("matte");
  material->setMaterialSourceUri(contract.sourceUri);
  material->setMaterialSourceSignature(contract.sourceSignature());
  material->setMaterialSourceReflectionHash(contract.reflectionHash);
  material->setMaterialContractReflection(std::move(contract));

  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{0.8f, 0.2f, 0.1f};
  material->setMaterialEnvelope(StringID("Kd"), std::move(kd));
  return material;
}

SceneResourceTableUploadView makeSourceMaterialUploadView(
    SceneResourceTable &sceneTable) {
  const MeshHandle mesh = sceneTable.registerMesh(makeTriangleMesh());
  const MaterialHandle material =
      sceneTable.registerMaterial(makeSourceMaterial());
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)sceneTable.registerObject(object);
  return sceneTable.buildUploadView();
}

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

void testGpuResourceTableConsumesSceneBindlessUploadView() {
  SceneResourceTable sceneTable;
  const SceneResourceTableUploadView view =
      makeSourceMaterialUploadView(sceneTable);
  VulkanGpuResourceTable gpu;

  const SceneBindlessUploadReport report =
      gpu.uploadSceneBindlessTables(view);

  EXPECT(report.diagnostics.empty(),
         "valid source material upload view should stage without diagnostics");
  EXPECT(report.textureSlots.size() == view.textures.size(),
         "every upload texture should receive a bindless slot");
  EXPECT(report.textureSlots.size() >= 3,
         "builtin default textures should be staged as bindless slots");
  EXPECT(report.materialStorageBuffers.size() ==
             view.sourceMaterialStorages.size(),
         "each source storage should produce one material storage buffer");
  EXPECT(!report.materialStorageBuffers.empty() &&
             report.materialStorageBuffers.front().sourceStorageIndex == 0,
         "material storage staging should preserve source storage index");
  EXPECT(!report.materialStorageBuffers.empty() &&
             report.materialStorageBuffers.front().sourceSignature ==
                 view.sourceMaterialStorages.front().sourceSignature,
         "material storage staging should preserve source signature for "
         "diagnostics");
  EXPECT(!report.materialStorageBuffers.empty() &&
             report.materialStorageBuffers.front().storageAbiHash ==
                 view.sourceMaterialStorages.front().storageAbiHash,
         "material storage staging should preserve storage ABI hash for "
         "diagnostics");
  EXPECT(!report.materialStorageBuffers.empty() &&
             report.materialStorageBuffers.front().byteSize ==
                 view.sourceMaterialRecords.front().bytes.size(),
         "material storage buffer byte size should match packed record bytes");
  EXPECT(report.objectBuffer.buffer.id != 0,
         "object table should create a staging buffer");
  EXPECT(report.objectBuffer.byteSize == sizeof(SceneGpuObjectRecord),
         "object table byte size should match one object record");
  EXPECT(report.drawBuffer.buffer.id != 0,
         "draw table should create a staging buffer");
  EXPECT(report.meshBuffer.buffer.id != 0,
         "mesh table should create a staging buffer");
  EXPECT(report.positionBuffer.buffer.id != 0,
         "position table should create a staging buffer");
  EXPECT(report.positionBuffer.byteSize == view.positions.size_bytes(),
         "position table byte size should match upload view positions");
  EXPECT(report.indexBuffer.buffer.id != 0,
         "index table should create a staging buffer");
  EXPECT(report.indexBuffer.byteSize == view.indices.size_bytes(),
         "index table byte size should match upload view indices");
  EXPECT(report.primitiveBuffer.buffer.id != 0,
         "primitive table should create a staging buffer");
  EXPECT(report.attributeStreamBuffer.buffer.id != 0,
         "attribute stream table should create a staging buffer");
  EXPECT(report.attributeValueBuffer.buffer.id != 0,
         "attribute value table should create a staging buffer");
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
  testGpuResourceTableConsumesSceneBindlessUploadView();
  testQueueCompilesStableIndirectBatches();
  testDescriptorChangeSplitsIndirectBatches();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless/indirect checks failed\n";
    return 1;
  }
  return 0;
}
