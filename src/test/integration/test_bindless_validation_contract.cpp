#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
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

void testStrictContractRejectsSkeletonDrawInputs() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(fixture.queue, StringID("Forward"));
  EXPECT(!result.ok,
         "Task 2 skeleton should reject draw inputs until preparation exists");
  EXPECT(result.coveredItemCount == 0,
         "Task 2 skeleton should not report covered inputs");
  EXPECT(result.diagnostics.size() == 2,
         "Task 2 skeleton should diagnose every draw input");
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

void testZeroIndexInputProducesSkeletonDiagnostic() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 0,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const RenderBatchAnalysis analysis = fixture.queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "Task 2 skeleton should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "Task 2 skeleton should produce exactly one diagnostic");
  EXPECT(analysis.batches.empty(),
         "Task 2 skeleton should not produce accepted batches");
  EXPECT(analysis.stats.unsupportedDrawCount == 1,
         "unsupported draw count should match rejected inputs");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::GlobalGeometryTableMissing,
           "Task 2 skeleton should report missing global geometry table");
  }
}

void testMissingDrawRecordInputProducesSkeletonDiagnostic() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});
  fixture.queue.clearItems();
  fixture.queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = ObjectHandle{},
      .mesh = MeshHandle{},
      .material = fixture.material,
      .debugId = StringID("helmet.missingObject"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  fixture.queue.prepareDrawInputs(fixture.table.buildUploadView());

  const RenderBatchAnalysis analysis = fixture.queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "Task 2 skeleton should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "Task 2 skeleton should produce exactly one diagnostic");
  EXPECT(analysis.batches.empty(),
         "Task 2 skeleton should not produce accepted batches");
  EXPECT(analysis.stats.unsupportedDrawCount == 1,
         "unsupported draw count should match rejected inputs");
  if (!analysis.diagnostics.empty()) {
    EXPECT(analysis.diagnostics.front().reason ==
               RenderBatchDiagnosticReason::GlobalGeometryTableMissing,
           "Task 2 skeleton should report missing global geometry table");
  }
}

} // namespace

int main() {
  testStrictContractRejectsSkeletonDrawInputs();
  testDecisionRejectsIncompleteMigratedWorkWithoutStrictMode();
  testStrictContractRejectsPartialCoverage();
  testMaterialV2StrictRejectsMissingFinalIdentity();
  testMaterialV2StrictRejectsMissingTypedSourceRef();
  testMaterialV2StrictDoesNotInferMaterialRefFallback();
  testZeroIndexInputProducesSkeletonDiagnostic();
  testMissingDrawRecordInputProducesSkeletonDiagnostic();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless validation contract checks failed\n";
    return 1;
  }
  return 0;
}
