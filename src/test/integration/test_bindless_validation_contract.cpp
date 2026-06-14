#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

struct TestResource final : public IGpuResource {
  TestResource(ResourceType type, StringID bindingName, u32 byteSize)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type = ResourceType::None;
  StringID bindingName;
  std::vector<u8> bytes;
};

struct ShaderWithBindings final : public IShader {
  explicit ShaderWithBindings(std::vector<ShaderResourceBinding> bindings)
      : m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const ShaderResourceBinding &candidate : m_bindings) {
      if (candidate.set == set && candidate.binding == binding) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const ShaderResourceBinding &candidate : m_bindings) {
      if (candidate.name == name) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 0; }

  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
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

ShaderResourceBinding storageBinding(const char *name) {
  ShaderResourceBinding binding;
  binding.name = name;
  binding.type = ShaderPropertyType::StorageBuffer;
  return binding;
}

RenderWorkItem makeMigratedDraw(const IGpuResource &vertex,
                                const IGpuResource &index) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  item.pass = StringID("Forward");
  item.debugId = StringID("bindless.validation.draw");
  item.objectSignature = StringID("bindless.validation.mesh");
  item.materialSignature = StringID("bindless.validation.material");
  item.materialTypeVariant = StringID("bindless.validation.materialTypeVariant");
  item.renderPathNodeSignature =
      StringID("bindless.validation.renderPathNode");
  RenderTargetDesc target;
  target.role = RenderTargetRole::Swapchain;
  target.colorFormat = ImageFormat::BGRA8;
  target.depthFormat = ImageFormat::D32Float;
  item.target = target;
  item.pipelineKey =
      PipelineKey::build(item.materialTypeVariant,
                         item.renderPathNodeSignature);
  item.raster.vertexBuffer = GpuResourceRef{vertex};
  item.raster.indexBuffer = GpuResourceRef{index};
  item.raster.indexCount = 3;
  item.raster.instanceCount = 1;
  return item;
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
        .debugId = StringID("helmet.validation"),
        .materialTypeSignature = desc.materialTypeSignature});
  }

  fixture.queue.prepareDrawInputs(fixture.table.buildUploadView());
  return fixture;
}

void testStrictContractAcceptsBatchedPreparedDrawInputs() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(fixture.queue, StringID("Forward"));
  EXPECT(result.ok,
         "fully batched prepared draw inputs should satisfy validation");
  EXPECT(result.coveredItemCount == 2,
         "validation should report compiler-emitted indirect draw coverage");
  EXPECT(result.diagnostics.empty(),
         "fully batched prepared draw inputs should not produce diagnostics");
}

void testDecisionAcceptsFullyBatchedMigratedWork() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const BindlessSubmissionDecision decision =
      decideBindlessSubmission(fixture.queue, StringID("Forward"), false, true);

  EXPECT(decision.kind == BindlessSubmissionDecisionKind::BindlessBatch,
         "fully batched migrated queue should choose bindless batch "
         "submission");
  EXPECT(decision.validation.ok,
         "accepted bindless batch submission should carry successful "
         "validation");
}

void testStrictContractRejectsPreparationDiagnostics() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 0,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(fixture.queue, StringID("Forward"));

  EXPECT(!result.ok,
         "validation should reject draw inputs with batch analysis diagnostics");
  EXPECT(result.coveredItemCount == 0,
         "rejected preparation diagnostics should not count as covered draws");
  EXPECT(result.diagnostics.size() == 1,
         "batch analysis diagnostics should translate into validation "
         "diagnostics");
}

void testStrictContractRejectsUnbatchedPreparedDrawInputs() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});
  RenderPathNodeData &nodeData =
      const_cast<RenderPathNodeData &>(fixture.queue.nodeData());
  nodeData.preparedCandidates.clear();

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(fixture.queue, StringID("Forward"));

  EXPECT(!result.ok,
         "validation should reject analysis whose draw stats do not cover all "
         "inputs");
  EXPECT(result.coveredItemCount == 0,
         "unbatched prepared inputs should not report covered draws");
}

void testStrictContractRejectsDuplicateCandidateInputCoverage() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});
  RenderPathNodeData &nodeData =
      const_cast<RenderPathNodeData &>(fixture.queue.nodeData());
  if (nodeData.preparedCandidates.size() == 2) {
    nodeData.preparedCandidates[1].inputIndex =
        nodeData.preparedCandidates[0].inputIndex;
  }

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(fixture.queue, StringID("Forward"));

  EXPECT(!result.ok,
         "validation should reject duplicate candidate coverage even when "
         "draw counts match");
  EXPECT(result.coveredItemCount == 1,
         "covered item count should count unique covered input identities");
  EXPECT(!result.diagnostics.empty(),
         "duplicate input coverage should produce validation diagnostics");
}

void testDecisionRejectsIncompleteMigratedWorkWithoutStrictMode() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkQueue queue;
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.raster.indexBuffer = GpuResourceRef{};
  queue.addItem(std::move(item));

  const BindlessSubmissionDecision decision =
      decideBindlessSubmission(queue, StringID("Forward"), false, true);
  EXPECT(decision.kind ==
             BindlessSubmissionDecisionKind::StrictValidationRejected,
         "incomplete migrated queue should be rejected without strict-mode "
         "fallback");
  EXPECT(!decision.validation.diagnostics.empty(),
         "rejected migrated queue should explain the incomplete draw");
  if (!decision.validation.diagnostics.empty()) {
    EXPECT(decision.validation.diagnostics.front().objectSignature ==
               StringID("bindless.validation.mesh"),
           "diagnostic should preserve object identity");
    EXPECT(decision.validation.diagnostics.front().materialSignature ==
               StringID("bindless.validation.material"),
           "diagnostic should preserve material identity");
  }
}

void testStrictContractRejectsPartialCoverage() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  RenderWorkQueue queue;
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.raster.indexBuffer = GpuResourceRef{};
  queue.addItem(std::move(item));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok, "strict bindless contract should reject partial coverage");
  EXPECT(!result.diagnostics.empty(),
         "strict bindless contract should report uncovered draw item");
}

void testMaterialV2StrictRejectsMissingFinalIdentity() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.shaderInfo = std::make_shared<ShaderWithBindings>(
      std::vector<ShaderResourceBinding>{storageBinding("SceneMaterials")});
  item.materialTypeVariant = StringID{};
  item.renderPathNodeSignature = StringID{};
  item.pipelineKey = PipelineKey{};
  item.raster.materialIndex = 0;
  item.raster.drawRecordIndex = 0;

  RenderWorkQueue queue;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "strict material v2 validation should reject missing final material "
         "source identities");
  EXPECT(!result.diagnostics.empty(),
         "missing final identities should produce diagnostics");
}

void testMaterialV2StrictRejectsMissingTypedSourceRef() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.shaderInfo = std::make_shared<ShaderWithBindings>(
      std::vector<ShaderResourceBinding>{storageBinding("SceneMaterialRefs"),
                                         storageBinding("SceneDraws")});
  item.raster.drawRecordIndex = 0;
  item.raster.materialRefIndex = u32_max;

  RenderWorkQueue queue;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "strict material v2 validation should reject missing typed material "
         "source ref index");
  EXPECT(!result.diagnostics.empty(),
         "missing typed material source ref should produce diagnostics");
}

void testMaterialV2StrictDoesNotInferMaterialRefFallback() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.shaderInfo = std::make_shared<ShaderWithBindings>(
      std::vector<ShaderResourceBinding>{storageBinding("SceneDraws")});
  item.raster.drawRecordIndex = 0;
  item.raster.materialRefIndex = u32_max;

  RenderWorkQueue queue;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(result.ok,
         "strict material v2 validation should not infer SceneMaterialRefs "
         "from the absence of SceneMaterials");
}

void testZeroIndexInputProducesDiagnostic() {
  ObjectHandle object;
  object.index = 4;
  object.generation = 1;
  MaterialHandle material;
  material.index = 8;
  material.generation = 1;
  MeshHandle meshHandle;
  meshHandle.index = 2;
  meshHandle.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = u32_max,
      .meshIndex = 0,
      .materialRefIndex = 0,
  }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 0,
      .indexCount = 0,
  }};
  std::array<SceneGpuMaterialRefRecord, 1> materialRefs{
      SceneGpuMaterialRefRecord{.sourceStorageIndex = 0,
                                .sourceLocalMaterialIndex = 0}};
  std::array<SourceLocalMaterialRecord, 1> sourceRecords{
      SourceLocalMaterialRecord{.sourceLocalMaterialIndex = 0}};
  std::array<SceneSourceLocalMaterialStorageView, 1> sourceStorages{
      SceneSourceLocalMaterialStorageView{.recordOffset = 0,
                                          .recordCount = 1}};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.materialRefs = std::span<const SceneGpuMaterialRefRecord>(materialRefs);
  view.sourceMaterialRecords =
      std::span<const SourceLocalMaterialRecord>(sourceRecords);
  view.sourceMaterialStorages =
      std::span<const SceneSourceLocalMaterialStorageView>(sourceStorages);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .mesh = meshHandle,
      .material = material,
      .debugId = StringID("helmet.zeroIndex"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "zero index count should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "zero index count should produce exactly one diagnostic");
  EXPECT(analysis.batches.empty(),
         "zero index count should not produce accepted batches");
  EXPECT(analysis.stats.unsupportedDrawCount == 1,
         "unsupported draw count should match rejected inputs");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::ZeroIndexCount,
           "zero index count diagnostic reason should be exact");
  }
}

void testMissingMaterialMappingDoesNotFallBackToDrawMaterialIndex() {
  ObjectHandle object;
  object.index = 1;
  object.generation = 1;
  MaterialHandle material;
  material.index = 7;
  material.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = 0,
      .meshIndex = 0,
      .materialRefIndex = 0,
  }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 0,
      .indexCount = 3,
  }};
  std::array<u32, 3> indices{0, 1, 2};
  std::array<SceneGpuMaterialRecord, 1> materials{};
  std::array<SceneGpuMaterialRefRecord, 1> materialRefs{
      SceneGpuMaterialRefRecord{.sourceStorageIndex = 0,
                                .sourceLocalMaterialIndex = 0}};
  std::array<SourceLocalMaterialRecord, 1> sourceRecords{
      SourceLocalMaterialRecord{.sourceLocalMaterialIndex = 0}};
  std::array<SceneSourceLocalMaterialStorageView, 1> sourceStorages{
      SceneSourceLocalMaterialStorageView{.recordOffset = 0,
                                          .recordCount = 1}};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.indices = std::span<const u32>(indices);
  view.materials = std::span<const SceneGpuMaterialRecord>(materials);
  view.materialRefs = std::span<const SceneGpuMaterialRefRecord>(materialRefs);
  view.sourceMaterialRecords =
      std::span<const SourceLocalMaterialRecord>(sourceRecords);
  view.sourceMaterialStorages =
      std::span<const SceneSourceLocalMaterialStorageView>(sourceStorages);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .material = material,
      .debugId = StringID("helmet.missingMaterialMapping"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(),
         "missing material handle mapping must reject instead of falling back "
         "to draw.materialIndex");
  EXPECT(analysis.candidates.empty(),
         "missing material handle mapping should not produce a candidate");
  EXPECT(analysis.diagnostics.size() == 1,
         "missing material handle mapping should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::InvalidDrawRecord,
           "missing material mapping with a draw material index should be an "
           "invalid draw record");
  }
}

void testInvisibleObjectProducesZeroInstanceDiagnostic() {
  ObjectHandle object;
  object.index = 2;
  object.generation = 1;
  MaterialHandle material;
  material.index = 3;
  material.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  objects[0].visible = 0;
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = u32_max,
      .meshIndex = 0,
      .materialRefIndex = 0,
  }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 0,
      .indexCount = 3,
  }};
  std::array<u32, 3> indices{0, 1, 2};
  std::array<SceneGpuMaterialRefRecord, 1> materialRefs{
      SceneGpuMaterialRefRecord{.sourceStorageIndex = 0,
                                .sourceLocalMaterialIndex = 0}};
  std::array<SourceLocalMaterialRecord, 1> sourceRecords{
      SourceLocalMaterialRecord{.sourceLocalMaterialIndex = 0}};
  std::array<SceneSourceLocalMaterialStorageView, 1> sourceStorages{
      SceneSourceLocalMaterialStorageView{.recordOffset = 0,
                                          .recordCount = 1}};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.indices = std::span<const u32>(indices);
  view.materialRefs = std::span<const SceneGpuMaterialRefRecord>(materialRefs);
  view.sourceMaterialRecords =
      std::span<const SourceLocalMaterialRecord>(sourceRecords);
  view.sourceMaterialStorages =
      std::span<const SceneSourceLocalMaterialStorageView>(sourceStorages);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .material = material,
      .debugId = StringID("helmet.zeroInstance"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "zero instance count should reject the draw");
  EXPECT(analysis.candidates.empty(),
         "zero instance count should not produce a candidate");
  EXPECT(analysis.diagnostics.size() == 1,
         "zero instance count should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::ZeroInstanceCount,
           "zero instance count diagnostic reason should be exact");
  }
}

void testMappedObjectIndexMissingObjectRowProducesUnresolvedDiagnostic() {
  ObjectHandle object;
  object.index = 5;
  object.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 2> draws{
      SceneGpuDrawRecord{},
      SceneGpuDrawRecord{
          .objectIndex = 1,
          .materialIndex = u32_max,
          .meshIndex = 0,
          .materialRefIndex = 0,
      }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 0,
      .indexCount = 3,
  }};
  std::array<u32, 3> indices{0, 1, 2};
  std::array<SceneGpuMaterialRefRecord, 1> materialRefs{
      SceneGpuMaterialRefRecord{.sourceStorageIndex = 0,
                                .sourceLocalMaterialIndex = 0}};
  std::array<SourceLocalMaterialRecord, 1> sourceRecords{
      SourceLocalMaterialRecord{.sourceLocalMaterialIndex = 0}};
  std::array<SceneSourceLocalMaterialStorageView, 1> sourceStorages{
      SceneSourceLocalMaterialStorageView{.recordOffset = 0,
                                          .recordCount = 1}};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 1}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.indices = std::span<const u32>(indices);
  view.materialRefs = std::span<const SceneGpuMaterialRefRecord>(materialRefs);
  view.sourceMaterialRecords =
      std::span<const SourceLocalMaterialRecord>(sourceRecords);
  view.sourceMaterialStorages =
      std::span<const SceneSourceLocalMaterialStorageView>(sourceStorages);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .debugId = StringID("helmet.missingUploadedObjectRow"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "missing uploaded object row should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "missing uploaded object row should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::ObjectDrawRecordUnresolved,
           "mapped object index outside view.objects should be unresolved, not "
           "an invalid draw row");
  }
}

void testMismatchedSourceMaterialHandleRejectsPreparation() {
  SceneResourceTable table;
  const MeshHandle mesh = table.registerMesh(makeIndexedMesh(3));
  const MaterialHandle materialA = table.registerMaterial(makeSourceMaterial());
  const MaterialHandle materialB = table.registerMaterial(makeSourceMaterial());

  ObjectResource object;
  object.mesh = mesh;
  object.material = materialA;
  object.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const ObjectHandle objectHandle = table.registerObject(object);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = objectHandle,
      .mesh = mesh,
      .material = materialB,
      .debugId = StringID("helmet.sourceMaterialMismatch"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(table.buildUploadView());

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(),
         "input material handle must match the source material ref used by "
         "the draw row");
  EXPECT(analysis.candidates.empty(),
         "mismatched source material handle should not produce a candidate");
  EXPECT(analysis.diagnostics.size() == 1,
         "mismatched source material handle should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::InvalidSourceMaterialRef,
           "mismatched source material handle should reject the source ref");
    EXPECT(analysis.diagnostics.front().materialRefIndex == 0,
           "diagnostic should preserve the draw row material ref index");
  }
}

void testMissingMeshRangeDiagnosticPreservesIndices() {
  ObjectHandle object;
  object.index = 6;
  object.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = u32_max,
      .meshIndex = 5,
      .materialRefIndex = 6,
  }};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .debugId = StringID("helmet.missingMeshRange"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "missing mesh range should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "missing mesh range should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    const RenderBatchDiagnostic &diagnostic = analysis.diagnostics.front();
    EXPECT(diagnostic.reason == RenderBatchDiagnosticReason::MissingMeshRange,
           "missing mesh row should report MissingMeshRange");
    EXPECT(diagnostic.drawRecordIndex == 0,
           "missing mesh diagnostic should preserve draw row index");
    EXPECT(diagnostic.materialRefIndex == 6,
           "missing mesh diagnostic should preserve material ref index");
    EXPECT(diagnostic.meshIndex == 5,
           "missing mesh diagnostic should preserve unresolved mesh index");
  }
}

void testInvalidSourceMaterialRefRowPreservesIndices() {
  ObjectHandle object;
  object.index = 7;
  object.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = u32_max,
      .meshIndex = 0,
      .materialRefIndex = 4,
  }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 0,
      .indexCount = 3,
  }};
  std::array<u32, 3> indices{0, 1, 2};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.indices = std::span<const u32>(indices);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .debugId = StringID("helmet.invalidSourceRefRow"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "invalid material ref row should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "invalid material ref row should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    const RenderBatchDiagnostic &diagnostic = analysis.diagnostics.front();
    EXPECT(diagnostic.reason ==
               RenderBatchDiagnosticReason::InvalidSourceMaterialRef,
           "invalid material ref row should report InvalidSourceMaterialRef");
    EXPECT(diagnostic.drawRecordIndex == 0,
           "invalid material ref diagnostic should preserve draw row index");
    EXPECT(diagnostic.materialRefIndex == 4,
           "invalid material ref diagnostic should preserve material ref "
           "index");
    EXPECT(diagnostic.meshIndex == 0,
           "invalid material ref diagnostic should preserve mesh index");
  }
}

void testMissingSourceStorageRowProducesInvalidSourceMaterialRef() {
  ObjectHandle object;
  object.index = 8;
  object.generation = 1;
  MaterialHandle material;
  material.index = 2;
  material.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = u32_max,
      .meshIndex = 0,
      .materialRefIndex = 0,
  }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 0,
      .indexCount = 3,
  }};
  std::array<u32, 3> indices{0, 1, 2};
  std::array<SceneGpuMaterialRefRecord, 1> materialRefs{
      SceneGpuMaterialRefRecord{.sourceStorageIndex = 3,
                                .sourceLocalMaterialIndex = 0}};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};
  std::array<SceneResourceMaterialRefUploadIndex, 1> materialRefMappings{
      SceneResourceMaterialRefUploadIndex{.handle = material, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.indices = std::span<const u32>(indices);
  view.materialRefs = std::span<const SceneGpuMaterialRefRecord>(materialRefs);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);
  view.materialRefIndexByHandle =
      std::span<const SceneResourceMaterialRefUploadIndex>(materialRefMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .material = material,
      .debugId = StringID("helmet.missingSourceStorage"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "missing source storage row should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "missing source storage row should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    const RenderBatchDiagnostic &diagnostic = analysis.diagnostics.front();
    EXPECT(diagnostic.reason ==
               RenderBatchDiagnosticReason::InvalidSourceMaterialRef,
           "missing source storage row should report InvalidSourceMaterialRef");
    EXPECT(diagnostic.drawRecordIndex == 0,
           "missing source storage diagnostic should preserve draw row index");
    EXPECT(diagnostic.materialRefIndex == 0,
           "missing source storage diagnostic should preserve material ref "
           "index");
    EXPECT(diagnostic.meshIndex == 0,
           "missing source storage diagnostic should preserve mesh index");
  }
}

void testMissingGlobalGeometryRangePreservesIndices() {
  ObjectHandle object;
  object.index = 9;
  object.generation = 1;

  std::array<SceneGpuObjectRecord, 1> objects{};
  std::array<SceneGpuDrawRecord, 1> draws{SceneGpuDrawRecord{
      .objectIndex = 0,
      .materialIndex = u32_max,
      .meshIndex = 0,
      .materialRefIndex = 0,
  }};
  std::array<SceneGpuMeshRecord, 1> meshes{SceneGpuMeshRecord{
      .vertexOffset = 0,
      .indexOffset = 3,
      .indexCount = 3,
  }};
  std::array<u32, 3> indices{0, 1, 2};
  std::array<SceneResourceObjectUploadIndex, 1> objectMappings{
      SceneResourceObjectUploadIndex{.handle = object, .typedIndex = 0}};

  SceneResourceTableUploadView view;
  view.objects = std::span<const SceneGpuObjectRecord>(objects);
  view.draws = std::span<const SceneGpuDrawRecord>(draws);
  view.meshes = std::span<const SceneGpuMeshRecord>(meshes);
  view.indices = std::span<const u32>(indices);
  view.objectIndexByHandle =
      std::span<const SceneResourceObjectUploadIndex>(objectMappings);

  RenderWorkQueue queue;
  queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = object,
      .debugId = StringID("helmet.missingGlobalGeometry"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  queue.prepareDrawInputs(view);

  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "missing global geometry range should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "missing global geometry range should produce one diagnostic");
  if (!analysis.diagnostics.empty()) {
    const RenderBatchDiagnostic &diagnostic = analysis.diagnostics.front();
    EXPECT(diagnostic.reason ==
               RenderBatchDiagnosticReason::GlobalGeometryTableMissing,
           "out-of-range global mesh indices should report global geometry "
           "missing");
    EXPECT(diagnostic.drawRecordIndex == 0,
           "global geometry diagnostic should preserve draw row index");
    EXPECT(diagnostic.materialRefIndex == 0,
           "global geometry diagnostic should preserve material ref index");
    EXPECT(diagnostic.meshIndex == 0,
           "global geometry diagnostic should preserve mesh index");
  }
}

void testMissingDrawRecordInputProducesDiagnostic() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});
  fixture.queue.clearItems();
  fixture.queue.setNodeContext(RenderPathNodeContext{
      .pass = StringID("Forward"),
      .renderPathNodeSignature = StringID("bindless.forward.opaque"),
      .target = RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                                 .colorFormat = ImageFormat::BGRA8,
                                 .depthFormat = ImageFormat::D32Float},
      .objectDataSignature = StringID("BindlessObjectData.v1")});
  fixture.queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = ObjectHandle{},
      .mesh = MeshHandle{},
      .material = fixture.material,
      .debugId = StringID("helmet.missingObject"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  fixture.queue.prepareDrawInputs(fixture.table.buildUploadView());

  const RenderBatchAnalysis analysis = fixture.queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "missing object/draw record should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "missing object/draw record should produce exactly one diagnostic");
  EXPECT(analysis.batches.empty(),
         "missing object/draw record should not produce accepted batches");
  EXPECT(analysis.stats.unsupportedDrawCount == 1,
         "unsupported draw count should match rejected inputs");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::ObjectDrawRecordUnresolved,
           "missing object record reason should be exact");
  }
}

} // namespace

int main() {
  testStrictContractAcceptsBatchedPreparedDrawInputs();
  testDecisionAcceptsFullyBatchedMigratedWork();
  testStrictContractRejectsPreparationDiagnostics();
  testStrictContractRejectsUnbatchedPreparedDrawInputs();
  testStrictContractRejectsDuplicateCandidateInputCoverage();
  testDecisionRejectsIncompleteMigratedWorkWithoutStrictMode();
  testStrictContractRejectsPartialCoverage();
  testMaterialV2StrictRejectsMissingFinalIdentity();
  testMaterialV2StrictRejectsMissingTypedSourceRef();
  testMaterialV2StrictDoesNotInferMaterialRefFallback();
  testZeroIndexInputProducesDiagnostic();
  testMissingMaterialMappingDoesNotFallBackToDrawMaterialIndex();
  testInvisibleObjectProducesZeroInstanceDiagnostic();
  testMappedObjectIndexMissingObjectRowProducesUnresolvedDiagnostic();
  testMismatchedSourceMaterialHandleRejectsPreparation();
  testMissingMeshRangeDiagnosticPreservesIndices();
  testInvalidSourceMaterialRefRowPreservesIndices();
  testMissingSourceStorageRowProducesInvalidSourceMaterialRef();
  testMissingGlobalGeometryRangePreservesIndices();
  testMissingDrawRecordInputProducesDiagnostic();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless validation contract checks failed\n";
    return 1;
  }
  return 0;
}
