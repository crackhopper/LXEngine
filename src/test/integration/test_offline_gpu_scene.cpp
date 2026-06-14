#include "core/asset/material_instance.hpp"
#include "core/asset/material_template.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "backend/vulkan/offline/offline_render_graph_executor.hpp"
#include "backend/vulkan/offline/software_compute_offline_integrator.hpp"

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

class FakeOfflineShader final : public IShader {
public:
  FakeOfflineShader() {
    m_stages.push_back(
        ShaderStageCode{ShaderStage::Compute, std::vector<u32>{0x07230203, 1}});
    m_bindings.push_back(ShaderResourceBinding{"OutputPixels",
                                               0,
                                               10,
                                               ShaderPropertyType::StorageBuffer,
                                               1,
                                               64,
                                               0,
                                               ShaderStage::Compute,
                                               {}});
  }

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
  usize getProgramHash() const override { return 0x730e2u; }
  std::string getShaderName() const override {
    return "offline_test_compute";
  }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

class FakeStorageResource final : public IGpuResource {
public:
  explicit FakeStorageResource(StringID bindingName)
      : m_bindingName(bindingName) {}

  ResourceType getType() const override { return ResourceType::StorageBuffer; }
  const void *getRawData() const override { return m_bytes.data(); }
  u32 getByteSize() const override {
    return static_cast<u32>(m_bytes.size());
  }
  StringID getBindingName() const override { return m_bindingName; }

private:
  StringID m_bindingName;
  std::vector<std::byte> m_bytes{std::byte{0}, std::byte{1}, std::byte{2},
                                 std::byte{3}};
};

MeshBufferUniquePtr makeTriangleMesh() {
  auto vertices = std::vector<SceneVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto vb = VertexBuffer<SceneVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create({0, 1, 2});
  return MeshBuffer::create(
             vb, ib,
             BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

CameraResource makeCameraResource() {
  const CameraPose pose =
      makeCameraPose(Vec3f{0.0f, 0.0f, 3.0f}, Vec3f{0.0f, 0.0f, -1.0f},
                     Vec3f{0.0f, 1.0f, 0.0f});
  const CameraProjection projection;
  return CameraResource{
      .pose = pose,
      .projection = projection,
      .view = makeCameraViewMatrix(pose),
      .proj = makeCameraProjectionMatrix(projection),
      .active = true,
  };
}

offline::OfflineRenderJob makeJob() {
  offline::OfflineRenderJob job;
  job.output.width = 17;
  job.output.height = 9;
  job.offline.samples = 1;
  job.offlineShader = std::make_shared<FakeOfflineShader>();

  const MeshHandle mesh = job.scene.registerMesh(makeTriangleMesh());
  auto material =
      MaterialInstance::createUnique(MaterialTemplate::create("offline_test"));
  material->setBsdfType("matte");
  material->syncGpuData();
  const MaterialHandle materialHandle =
      job.scene.registerMaterial(std::move(material));

  ObjectResource object;
  object.mesh = mesh;
  object.material = materialHandle;
  object.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)job.scene.registerObject(object);
  (void)job.scene.registerCamera(makeCameraResource());
  return job;
}

bool hasDescriptorBinding(const RenderInputDesc &desc, StringID bindingName) {
  for (const DescriptorResourceRef &descriptor :
       desc.bindingPlan.descriptors) {
    if (descriptor.getBindingName() == bindingName) {
      return true;
    }
  }
  return false;
}

void testOfflineGraphDeclaresComputeInput() {
  const offline::OfflineRenderJob job = makeJob();
  const FrameGraph graph = offline::createOfflineRenderFrameGraph(job.output);

  EXPECT(graph.getPasses().size() == 1,
         "offline graph should contain one pass");
  const FramePass &pass = graph.getPasses().front();
  EXPECT(pass.stage == RenderPassStage::Compute,
         "offline graph pass should be compute stage");
  EXPECT(pass.dispatch == RenderPassDispatch::Compute,
         "offline graph pass should be compute dispatch");
  EXPECT(pass.input.kind == RenderPassInputKind::ComputeDispatch,
         "offline graph pass should request compute input");
}

void testOfflineGraphProducesAcceptedComputeDesc() {
  offline::OfflineRenderJob job = makeJob();
  FrameGraph graph = offline::createOfflineRenderFrameGraph(job.output);
  const FramePass &pass = graph.getPasses().front();

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context = RenderWorkBuildContext::offline(job);
  compiler.buildInputs(pass, context, inputs);
  const std::vector<RenderInputDesc> descs =
      compiler.prepare(pass, context, inputs);

  EXPECT(inputs.size() == 1, "offline compiler should produce one input");
  EXPECT(descs.size() == 1, "offline compiler should produce one desc");
  EXPECT(descs.front().accepted(), "offline compute desc should be accepted");
  EXPECT(descs.front().pipelineBuildDesc.type == PipelineBuildType::Compute,
         "offline desc should carry compute pipeline facts");
  EXPECT(hasDescriptorBinding(descs.front(), StringID("OutputPixels")),
         "offline desc should bind output storage");
  EXPECT(descs.front().stats.compilerInputCount == 1,
         "offline stats should count compiler inputs");
  EXPECT(descs.front().stats.acceptedInputCount == 1,
         "offline stats should count accepted inputs");
  EXPECT(descs.front().stats.submittedDispatchCount == 1,
         "offline stats should count dispatch submission");

  const auto *compute = dynamic_cast<const RenderComputeInput *>(inputs.front().get());
  EXPECT(compute != nullptr, "offline input should be compute typed");
  if (compute != nullptr) {
    EXPECT(compute->groupCountX == 3,
           "offline dispatch X groups should ceil-divide width by 8");
    EXPECT(compute->groupCountY == 2,
           "offline dispatch Y groups should ceil-divide height by 8");
  }
}

void testOfflineJobValidationRejectsMissingCamera() {
  offline::OfflineRenderJob job = makeJob();
  job.scene = SceneResourceTable{};
  bool threw = false;
  try {
    offline::validateOfflineRenderJob(job);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  EXPECT(threw, "offline validation should reject missing scene data");
}

void testComputeReadbackUsesInputDeclaredBinding() {
  FakeStorageResource customOutput(StringID("CustomOutput"));
  RenderComputeInput input;
  input.readbackResource = StringID("CustomOutput");

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.bindingPlan.descriptors.emplace_back(customOutput);

  const GpuResourceRef resolved =
      backend::offline::resolveComputeReadbackResource(input, desc);

  EXPECT(resolved.isValid(),
         "readback resolver should find custom compute output binding");
  if (resolved.isValid()) {
    EXPECT(resolved.getBindingName() == StringID("CustomOutput"),
           "readback resolver should use RenderComputeInput::readbackResource");
  }
}

void testComputeReadbackRejectsMissingInputBindingName() {
  RenderComputeInput input;
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;

  bool threw = false;
  try {
    (void)backend::offline::resolveComputeReadbackResource(input, desc);
  } catch (const std::runtime_error &error) {
    threw = std::string(error.what()).find("readback resource") !=
            std::string::npos;
  }

  EXPECT(threw, "readback resolver should reject missing readback binding");
}

void testComputeReadbackRejectsNonResourceBinding() {
  RenderComputeInput input;
  input.readbackResource = StringID("CustomOutput");

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.bindingPlan.descriptors.push_back(DescriptorResourceRef::textureArray(
      StringID("CustomOutput"), std::vector<TextureSamplerRef>{}));

  bool threw = false;
  try {
    (void)backend::offline::resolveComputeReadbackResource(input, desc);
  } catch (const std::runtime_error &error) {
    threw = std::string(error.what()).find("not a resource") !=
            std::string::npos;
  }

  EXPECT(threw, "readback resolver should reject non-resource binding");
}

void testPreparedOfflineDescValidationThrowsDiagnosticDetail() {
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.pass = StringID("OfflineCompute");
  desc.debugId = StringID("accepted.with.diagnostic");
  desc.diagnostics.push_back(RenderInputDiagnostic{
      .code = RenderInputDiagnosticCode::MissingResource,
      .pass = desc.pass,
      .debugId = desc.debugId,
      .message = "custom offline diagnostic"});

  bool threw = false;
  try {
    backend::offline::validatePreparedOfflineRenderDescs({{desc}});
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    threw = message.find("custom offline diagnostic") != std::string::npos &&
            message.find("OfflineCompute") != std::string::npos &&
            message.find("accepted.with.diagnostic") != std::string::npos;
  }

  EXPECT(threw,
         "offline desc validation should throw diagnostic pass/debug details");
}

void testPreparedOfflineDescValidationRejectsDiagnosticlessRejectedDesc() {
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Rejected;
  desc.pass = StringID("OfflineCompute");
  desc.debugId = StringID("rejected.without.diagnostic");

  bool threw = false;
  try {
    backend::offline::validatePreparedOfflineRenderDescs({{desc}});
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    threw = message.find("rejected prepared offline render desc") !=
                std::string::npos &&
            message.find("OfflineCompute") != std::string::npos &&
            message.find("rejected.without.diagnostic") != std::string::npos;
  }

  EXPECT(threw,
         "offline desc validation should reject descs without diagnostics");
}

} // namespace

int main() {
  testOfflineGraphDeclaresComputeInput();
  testOfflineGraphProducesAcceptedComputeDesc();
  testOfflineJobValidationRejectsMissingCamera();
  testComputeReadbackUsesInputDeclaredBinding();
  testComputeReadbackRejectsMissingInputBindingName();
  testComputeReadbackRejectsNonResourceBinding();
  testPreparedOfflineDescValidationThrowsDiagnosticDetail();
  testPreparedOfflineDescValidationRejectsDiagnosticlessRejectedDesc();
  if (g_failures != 0) {
    std::cerr << g_failures << " offline gpu scene checks failed\n";
    return 1;
  }
  return 0;
}
