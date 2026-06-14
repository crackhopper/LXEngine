#include "backend/vulkan/vulkan_gpu_resource_table.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
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

MeshBufferUniquePtr makeIndexedMesh(u32 indexCount) {
  auto vertices = std::vector<SceneVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  std::vector<u32> indices;
  indices.reserve(indexCount);
  for (u32 i = 0; i < indexCount; ++i) {
    indices.push_back(i % 3);
  }
  auto vb = VertexBuffer<SceneVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
             vb, ib,
             BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

struct BatchQueueFixtureDesc final {
  usize drawCount = 0;
  u32 indexCount = 0;
  StringID materialTypeSignature;
};

struct BatchQueueFixture final {
  SceneResourceTable table;
  RenderWorkQueue queue;
  MaterialHandle material;
};

BatchQueueFixture makeBatchQueueFixture(const BatchQueueFixtureDesc &desc) {
  BatchQueueFixture fixture;
  const MeshHandle mesh = fixture.table.registerMesh(
      makeIndexedMesh(desc.indexCount));
  fixture.material = fixture.table.registerMaterial(makeSourceMaterial());

  fixture.queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});

  for (usize i = 0; i < desc.drawCount; ++i) {
    ObjectResource object;
    object.mesh = mesh;
    object.material = fixture.material;
    object.worldBounds =
        BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    const ObjectHandle objectHandle = fixture.table.registerObject(object);

    fixture.queue.addDrawInput(RenderDrawInput{
        .inputIndex = i,
        .object = objectHandle,
        .mesh = mesh,
        .material = fixture.material,
        .debugId = StringID("helmet.sameSignature"),
        .materialTypeSignature = desc.materialTypeSignature});
  }

  fixture.queue.prepareDrawInputs(fixture.table.buildUploadView());
  return fixture;
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

void testLegacyRenderWorkItemsAreNotAcceptedAsBatchAnalysis() {
  TestGpuResource vertex(ResourceType::VertexBuffer, StringID{}, 128);
  TestGpuResource index(ResourceType::IndexBuffer, StringID{}, 96);
  TestGpuResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"),
                         64);
  const PipelineKey key =
      PipelineKey::build(StringID("bindless.indirect.materialTypeVariant"),
                         StringID("bindless.indirect.renderPathNode"));

  RenderWorkQueue queue;
  queue.addItem(makeDrawItem(vertex, index, camera, key, 0));
  queue.addItem(makeDrawItem(vertex, index, camera, key, 6));

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();
  EXPECT(analysis.batches.empty(),
         "legacy RenderWorkItem inputs should not produce analysis batches");
  EXPECT(analysis.stats.inputDrawCount == 0,
         "legacy RenderWorkItem inputs should not become draw inputs");

  const BindlessValidationResult validation =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!validation.ok,
         "legacy RenderWorkItem geometry should be rejected by validation");
  EXPECT(validation.coveredItemCount == 0,
         "legacy RenderWorkItem geometry should not count as covered");
  EXPECT(validation.diagnostics.size() == 2,
         "each legacy RenderWorkItem should produce a validation diagnostic");
}

void testSameSignatureInputsProducePreparedCandidates() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const RenderBatchAnalysis analysis = fixture.queue.compileIndirectBatches();

  EXPECT(analysis.ok(),
         "valid same-signature inputs should prepare without diagnostics");
  EXPECT(analysis.candidates.size() == 2,
         "valid same-signature inputs should produce prepared candidates");
  EXPECT(analysis.batches.empty(),
         "Task 4 preparation should not merge accepted batches yet");
  EXPECT(analysis.diagnostics.empty(),
         "valid same-signature inputs should not keep skeleton diagnostics");
  EXPECT(analysis.stats.inputDrawCount == 2,
         "analysis stats should preserve input draw count");
  EXPECT(analysis.stats.preparedCandidateCount == 2,
         "prepared candidate count should match valid inputs");
  EXPECT(analysis.stats.unsupportedDrawCount == 0,
         "unsupported draw count should stay zero for valid preparation");
  for (usize i = 0; i < analysis.candidates.size(); ++i) {
    const PreparedRenderDrawCandidate &candidate = analysis.candidates[i];
    EXPECT(candidate.inputIndex == i,
           "candidate should preserve input draw index");
    EXPECT(candidate.objectIndex == i,
           "candidate should resolve object handle through upload view");
    EXPECT(candidate.drawRecordIndex == i,
           "candidate draw record should derive from object typed index");
    EXPECT(candidate.meshIndex == 0,
           "candidate should resolve the shared mesh table index");
    EXPECT(candidate.materialIndex == u32_max,
           "source-contract candidate should not invent a legacy material "
           "index");
    EXPECT(candidate.indexCount == 3,
           "candidate should copy mesh index count from global table range");
    EXPECT(candidate.firstIndex == 0,
           "candidate should copy mesh first index from global table range");
    EXPECT(candidate.vertexOffset == 0,
           "candidate should copy mesh vertex offset from global table range");
    EXPECT(candidate.instanceCount == 1,
           "candidate should be prepared as one instance per draw input");
    EXPECT(candidate.materialRefIndex == 0,
           "candidate should resolve source material ref table index");
    EXPECT(candidate.sourceStorageIndex == 0,
           "candidate should resolve source material storage index");
    EXPECT(candidate.sourceLocalMaterialIndex == 0,
           "candidate should resolve source-local material record index");
    EXPECT(candidate.materialTypeSignature ==
               StringID("standard-pbr-opaque"),
           "candidate should preserve material type signature");
    EXPECT(candidate.objectDataSignature ==
               StringID("BindlessObjectData.v1"),
           "candidate should carry node object data ABI signature");
  }
  EXPECT(analysis.stats.fallbackObservedCount == 0,
         "new batch compiler must not report old fallback usage");
}

void testDrawInputIdentityIsPreservedInDiagnostics() {
  SceneResourceTable table;
  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 42,
      .debugId = StringID("helmet.explicitInputIndex"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(table.buildUploadView());

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(analysis.diagnostics.size() == 1,
         "single input should produce a single diagnostic");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().inputIndex == 42,
           "diagnostic should preserve explicit draw input identity");
  }
}

void testClearItemsResetsNodeContextAndCachedAnalysis() {
  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .debugId = StringID("helmet.resetInput"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  (void)queue.compileIndirectBatches();

  queue.clearItems();
  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(analysis.context.pass.id == 0,
         "clearItems should reset node context");
  EXPECT(analysis.stats.inputDrawCount == 0,
         "clearItems should remove draw inputs");
  EXPECT(analysis.diagnostics.empty(),
         "clearItems should invalidate cached diagnostics");
  EXPECT(queue.lastBatchAnalysis().diagnostics.empty(),
         "last batch analysis should reflect the fresh empty compile");
}

void testDrawInputMutationInvalidatesCachedAnalysis() {
  SceneResourceTable table;
  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .debugId = StringID("helmet.cachedInput"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(table.buildUploadView());
  (void)queue.compileIndirectBatches();
  EXPECT(!queue.lastBatchAnalysis().diagnostics.empty(),
         "compiled analysis should be cached");

  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 1,
      .debugId = StringID("helmet.newInput"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});

  EXPECT(queue.lastBatchAnalysis().diagnostics.empty(),
         "mutating draw inputs should invalidate cached analysis");
}

void testStalePreparationRejectsNewDrawInputs() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  fixture.queue.addDrawInput(RenderDrawInput{
      .inputIndex = 1,
      .debugId = StringID("helmet.unpreparedInput"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});

  const RenderBatchAnalysis analysis = fixture.queue.compileIndirectBatches();

  EXPECT(!analysis.ok(),
         "adding a draw input after preparation should reject stale "
         "preparation");
  EXPECT(analysis.candidates.empty(),
         "stale preparation should not expose old prepared candidates");
  EXPECT(analysis.diagnostics.size() == 2,
         "stale preparation should diagnose every current draw input");
  if (analysis.diagnostics.size() == 2) {
    EXPECT(analysis.diagnostics[0].inputIndex == 0,
           "stale preparation diagnostic should cover first input");
    EXPECT(analysis.diagnostics[1].inputIndex == 1,
           "stale preparation diagnostic should cover newly added input");
  }
  EXPECT(analysis.stats.inputDrawCount == 2,
         "stale preparation analysis should preserve current draw count");
  EXPECT(analysis.stats.unsupportedDrawCount == 2,
         "stale preparation unsupported count should cover all inputs");
}

} // namespace

int main() {
  testSharedTextureIdentityProducesOneBindlessSlot();
  testDifferentTextureIdentityProducesDifferentBindlessSlots();
  testPipelineCacheFindAndCreateAreObservable();
  testProgressTracksResourceWork();
  testIndirectDrawBufferHandleIsOpaque();
  testGpuResourceTableConsumesSceneBindlessUploadView();
  testLegacyRenderWorkItemsAreNotAcceptedAsBatchAnalysis();
  testSameSignatureInputsProducePreparedCandidates();
  testDrawInputIdentityIsPreservedInDiagnostics();
  testClearItemsResetsNodeContextAndCachedAnalysis();
  testDrawInputMutationInvalidatesCachedAnalysis();
  testStalePreparationRejectsNewDrawInputs();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless/indirect checks failed\n";
    return 1;
  }
  return 0;
}
