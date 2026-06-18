#include "core/asset/texture.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

template <typename T>
concept HasDrawCommands = requires(T value) { value.drawCommands; };

template <typename T>
concept HasGroupCounts = requires(T value) {
  value.groupCountX;
  value.groupCountY;
  value.groupCountZ;
};

template <typename T>
concept HasReadbackResource = requires(T value) { value.readbackResource; };

template <typename T>
concept HasShaderProgram = requires(T value) { value.shaderProgram; };

template <typename T>
concept HasShaderInfo = requires(T value) { value.shaderInfo; };

template <typename T>
concept HasVertexLayout = requires(T value) { value.vertexLayout; };

template <typename T>
concept HasRenderState = requires(T value) { value.renderState; };

template <typename T>
concept HasTopology = requires(T value) { value.topology; };

template <typename T>
concept HasDescriptorResources =
    requires(T value) { value.descriptorResources; };

ShaderResourceBinding makeUniformBinding(std::string name, u32 binding = 0) {
  return ShaderResourceBinding{std::move(name),
                               0,
                               binding,
                               ShaderPropertyType::UniformBuffer,
                               1,
                               192,
                               0,
                               ShaderStage::Vertex,
                               {}};
}

ShaderResourceBinding makeTextureBinding(std::string name, u32 binding = 0) {
  return ShaderResourceBinding{
      std::move(name),       0, binding, ShaderPropertyType::Texture2D, 1, 0, 0,
      ShaderStage::Fragment, {}};
}

ShaderResourceBinding makeTextureCubeBinding(std::string name,
                                             u32 binding = 0) {
  return ShaderResourceBinding{std::move(name),
                               1,
                               binding,
                               ShaderPropertyType::TextureCube,
                               1,
                               0,
                               0,
                               ShaderStage::Fragment,
                               {}};
}

ShaderResourceBinding makeComputeStorageBinding(std::string name,
                                                u32 binding = 0) {
  return ShaderResourceBinding{std::move(name),
                               0,
                               binding,
                               ShaderPropertyType::StorageBuffer,
                               1,
                               64,
                               0,
                               ShaderStage::Compute,
                               {}};
}

struct TestGpuResource final : public IGpuResource {
  TestGpuResource(ResourceType type, StringID bindingName, u32 byteSize = 64)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type;
  StringID bindingName;
  std::vector<u8> bytes;
};

class MateriallessRenderable final : public IRenderable {
public:
  explicit MateriallessRenderable(std::string nodeName,
                                  bool passSupported = true)
      : m_nodeName(std::move(nodeName)),
        m_debugId(StringID("debug." + m_nodeName)),
        m_passSupported(passSupported) {}

  void setDebugOnly(bool value) { m_debugOnly = value; }
  void setRenderType(StringID renderType) { m_renderType = renderType; }
  void setIndexedTriangle() {
    m_vertexBuffer = VertexBuffer<VertexPos>::create(
        std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
    m_indexBuffer = IndexBuffer::create({0, 1, 2});
  }

  GpuResourceRef getVertexBuffer() const override {
    return m_vertexBuffer ? GpuResourceRef{*m_vertexBuffer} : GpuResourceRef{};
  }
  GpuResourceRef getIndexBuffer() const override {
    return m_indexBuffer ? GpuResourceRef{*m_indexBuffer} : GpuResourceRef{};
  }
  IShaderSharedPtr getShaderInfo() const override { return nullptr; }
  StringID getPipelineSignature(StringID pass) const override { return pass; }
  bool supportsPass(StringID) const override { return m_passSupported; }
  VisibilityLayerMask getVisibilityLayerMask() const override {
    return VisibilityMask_All;
  }
  std::string getNodeName() const override { return m_nodeName; }
  StringID getDebugId() const override { return m_debugId; }
  bool isDebugOnlyRenderable() const override { return m_debugOnly; }
  std::optional<StringID> getRenderType() const override {
    return m_renderType;
  }

  std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID) const override {
    return std::nullopt;
  }

private:
  std::string m_nodeName;
  StringID m_debugId;
  bool m_passSupported = true;
  bool m_debugOnly = false;
  std::optional<StringID> m_renderType;
  VertexBufferSharedPtr m_vertexBuffer;
  IndexBufferSharedPtr m_indexBuffer;
};

class FakeShader final : public IShader {
public:
  FakeShader(std::vector<ShaderResourceBinding> bindings,
             std::vector<ShaderStageCode> stages,
             std::vector<VertexInputAttribute> vertexInputs = {},
             std::vector<ShaderSpecializationConstantInfo>
                 specializationConstants = {})
      : m_bindings(std::move(bindings)), m_stages(std::move(stages)),
        m_vertexInputs(std::move(vertexInputs)),
        m_specializationConstants(std::move(specializationConstants)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }
  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }
  const std::vector<VertexInputAttribute> &getVertexInputs() const override {
    return m_vertexInputs;
  }
  const std::vector<ShaderSpecializationConstantInfo> &
  getSpecializationConstants() const override {
    return m_specializationConstants;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    const auto it = std::find_if(m_bindings.begin(), m_bindings.end(),
                                 [&](const ShaderResourceBinding &candidate) {
                                   return candidate.set == set &&
                                          candidate.binding == binding;
                                 });
    if (it == m_bindings.end()) {
      return std::nullopt;
    }
    return std::cref(*it);
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    const auto it = std::find_if(m_bindings.begin(), m_bindings.end(),
                                 [&](const ShaderResourceBinding &candidate) {
                                   return candidate.name == name;
                                 });
    if (it == m_bindings.end()) {
      return std::nullopt;
    }
    return std::cref(*it);
  }
  usize getProgramHash() const override { return 0x1234u; }
  std::string getShaderName() const override { return "validated_fake_shader"; }

private:
  std::vector<ShaderResourceBinding> m_bindings;
  std::vector<ShaderStageCode> m_stages;
  std::vector<VertexInputAttribute> m_vertexInputs;
  std::vector<ShaderSpecializationConstantInfo> m_specializationConstants;
};

class ValidatedRenderable final : public IRenderable {
public:
  explicit ValidatedRenderable(StringID pass, std::string variantValue = "1",
                               std::string materialTypeSignatureName =
                                   "validated.renderable.material.type",
                               std::string nodeName = "validated_renderable",
                               std::vector<ShaderResourceBinding> bindings = {})
      : m_pass(pass), m_nodeName(std::move(nodeName)),
        m_debugId(StringID("debug." + m_nodeName)) {
    m_vertexBuffer = VertexBuffer<VertexPos>::create(
        std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
    m_indexBuffer =
        IndexBuffer::create({0, 1, 2}, PrimitiveTopology::TriangleList);

    m_bindings = std::move(bindings);
    m_stages = {
        ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203, 11}},
        ShaderStageCode{ShaderStage::Fragment,
                        std::vector<u32>{0x07230203, 12}},
    };
    m_shader = std::make_shared<FakeShader>(m_bindings, m_stages);
    m_shaderProgram.shaderName = "validated_fake_shader";
    m_shaderProgram.shader = m_shader;
    m_shaderProgram.variants.push_back(ShaderVariant{
        .macroName = "LX_TEST_VARIANT",
        .enabled = true,
        .macroValue = std::move(variantValue),
    });

    m_data.pass = m_pass;
    m_data.shaderProgram = m_shaderProgram;
    m_data.shaderInfo = m_shader;
    m_data.vertexBuffer = GpuResourceRef{*m_vertexBuffer};
    m_data.indexBuffer = GpuResourceRef{*m_indexBuffer};
    m_data.renderState.cullMode = CullMode::Front;
    m_data.renderState.depthTestEnable = false;
    m_data.materialTypeSignature =
        StringID(std::move(materialTypeSignatureName));
    m_data.materialTypeVariant = m_shaderProgram.getPipelineSignature();
  }

  const std::vector<ShaderStageCode> &stages() const { return m_stages; }
  const std::vector<ShaderResourceBinding> &bindings() const {
    return m_bindings;
  }
  const ShaderProgramSet &shaderProgram() const { return m_shaderProgram; }
  const IShaderSharedPtr &shaderInfo() const { return m_shader; }
  const VertexLayout &vertexLayout() const {
    return m_vertexBuffer->getLayout();
  }

  GpuResourceRef getVertexBuffer() const override {
    return GpuResourceRef{*m_vertexBuffer};
  }
  GpuResourceRef getIndexBuffer() const override {
    return GpuResourceRef{*m_indexBuffer};
  }
  IShaderSharedPtr getShaderInfo() const override { return m_shader; }
  StringID getPipelineSignature(StringID) const override {
    return m_shaderProgram.getPipelineSignature();
  }
  bool supportsPass(StringID pass) const override { return pass == m_pass; }
  VisibilityLayerMask getVisibilityLayerMask() const override {
    return VisibilityMask_All;
  }
  std::string getNodeName() const override { return m_nodeName; }
  StringID getDebugId() const override { return m_debugId; }

  std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const override {
    if (pass == m_pass) {
      return std::cref(m_data);
    }
    return std::nullopt;
  }

private:
  StringID m_pass;
  std::string m_nodeName;
  StringID m_debugId;
  VertexBufferSharedPtr m_vertexBuffer;
  IndexBufferSharedPtr m_indexBuffer;
  std::vector<ShaderResourceBinding> m_bindings;
  std::vector<ShaderStageCode> m_stages;
  IShaderSharedPtr m_shader;
  ShaderProgramSet m_shaderProgram;
  ValidatedRenderablePassData m_data;
};

bool hasDiagnosticCode(const RenderInputDesc &desc,
                       RenderInputDiagnosticCode code) {
  return std::any_of(desc.diagnostics.begin(), desc.diagnostics.end(),
                     [code](const RenderInputDiagnostic &diagnostic) {
                       return diagnostic.code == code;
                     });
}

bool hasDiagnosticMessage(const RenderInputDesc &desc,
                          const std::string &message) {
  return std::any_of(desc.diagnostics.begin(), desc.diagnostics.end(),
                     [&](const RenderInputDiagnostic &diagnostic) {
                       return diagnostic.message.find(message) !=
                              std::string::npos;
                     });
}

bool hasFatalPipelineDiagnostic(const RenderInputDesc &desc) {
  return hasDiagnosticCode(
             desc, RenderInputDiagnosticCode::MissingShaderReflection) ||
         hasDiagnosticCode(desc,
                           RenderInputDiagnosticCode::MissingPipelineFacts);
}

bool hasDescriptorBindingName(const RenderInputDesc &desc,
                              StringID bindingName) {
  return std::any_of(desc.bindingPlan.descriptors.begin(),
                     desc.bindingPlan.descriptors.end(),
                     [bindingName](const DescriptorResourceRef &descriptor) {
                       return descriptor.getBindingName() == bindingName;
                     });
}

bool hasResourceDependencyBindingName(const RenderInputDesc &desc,
                                      StringID bindingName) {
  return std::any_of(
      desc.resourceDependencies.begin(), desc.resourceDependencies.end(),
      [bindingName](const GpuResourceRef &resource) {
        return resource.isValid() && resource.getBindingName() == bindingName;
      });
}

bool approxEqual(float lhs, float rhs, float epsilon = 0.0001f) {
  return std::abs(lhs - rhs) <= epsilon;
}

MeshSharedPtr makeSceneTriangleMesh(float extent = 1.0f) {
  return Mesh::create(VertexBuffer<VertexPos>::create(std::vector<VertexPos>{
                          {{0.0f, 0.0f, 0.0f}},
                          {{extent, 0.0f, 0.0f}},
                          {{0.0f, extent, 0.0f}},
                      }),
                      IndexBuffer::create({0, 1, 2}),
                      BoundingBox{{0.0f, 0.0f, 0.0f}, {extent, extent, 0.0f}});
}

MaterialInstanceSharedPtr makeSceneMaterial(const std::string &name) {
  return MaterialInstance::create(MaterialTemplate::create(name));
}

MaterialInstanceSharedPtr makeTexturedSceneMaterial(const std::string &name) {
  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureBinding("BaseColorMap")},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 91}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 92}},
      });
  ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = name + "_shader";
  shaderProgram.shader = shader;

  MaterialPassDefinition pass;
  pass.shaderProgram = shaderProgram;
  auto materialTemplate = MaterialTemplate::create(name);
  materialTemplate->setPassDefinition(Pass_Forward, pass);
  materialTemplate->rebuildMaterialInterface();

  auto material = MaterialInstance::create(materialTemplate);
  TextureDesc textureDesc;
  textureDesc.width = 1;
  textureDesc.height = 1;
  textureDesc.format = TextureFormat::RGBA8;
  auto texture = std::make_shared<Texture>(textureDesc,
                                           std::vector<u8>{255, 255, 255, 255});
  material->setTexture(StringID("BaseColorMap"),
                       std::make_shared<CombinedTextureSampler>(texture));
  return material;
}

void expectAcceptedDescHasBackendPipelineFacts(const RenderInputDesc &desc,
                                               const char *context) {
  EXPECT(desc.accepted(), context);
  EXPECT(!hasFatalPipelineDiagnostic(desc),
         "accepted desc must not carry fatal pipeline diagnostics");
  EXPECT(desc.pipelineBuildDesc.key == desc.pipelineKey,
         "accepted desc pipeline build key should match desc key");
  EXPECT(desc.pipelineBuildDesc.key.id.id != 0,
         "accepted desc should carry non-empty pipeline build key");
  EXPECT(!desc.pipelineBuildDesc.stages.empty(),
         "accepted desc should carry shader stages for backend build");
}

void testRenderInputPayloadDoesNotExposePipelineFacts() {
  EXPECT(!HasShaderProgram<RenderDrawInput>,
         "draw input should not expose shader program facts");
  EXPECT(!HasShaderInfo<RenderDrawInput>,
         "draw input should not expose shader reflection facts");
  EXPECT(!HasVertexLayout<RenderDrawInput>,
         "draw input should not expose pipeline vertex layout facts");
  EXPECT(!HasRenderState<RenderDrawInput>,
         "draw input should not expose render state pipeline facts");
  EXPECT(!HasTopology<RenderDrawInput>,
         "draw input should not expose topology pipeline facts");
  EXPECT(!HasDescriptorResources<RenderDrawInput>,
         "draw input should not expose descriptor binding facts");

  EXPECT(!HasShaderProgram<RenderComputeInput>,
         "compute input should not expose shader program facts");
  EXPECT(!HasShaderInfo<RenderComputeInput>,
         "compute input should not expose shader reflection facts");
  EXPECT(!HasDescriptorResources<RenderComputeInput>,
         "compute input should not expose descriptor binding facts");
}

void testDescReferencesInputWithoutOwningPayload() {
  RenderDrawInput input;
  input.inputIndex = 0;
  input.source = RenderDrawInputSource::FullscreenTriangle;
  input.drawCommands.push_back(RenderDrawCommand{.indexCount = 3});

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;

  EXPECT(desc.accepted(), "desc should expose accepted state");
  EXPECT(desc.inputIndex == 0, "desc should reference input by index");
  EXPECT(input.drawCommands.size() == 1,
         "draw command payload should remain on input");
  EXPECT(!HasDrawCommands<RenderInputDesc>,
         "desc should not own draw command payload");
}

void testDescStatusDefaultsAndStats() {
  RenderInputDesc desc;
  EXPECT(!desc.accepted(), "desc should default to rejected");
  EXPECT(desc.status == RenderInputStatus::Rejected,
         "desc status should default to rejected");

  desc.status = RenderInputStatus::Accepted;
  EXPECT(desc.accepted(), "desc accepted() should track accepted status");

  desc.diagnostics.push_back(RenderInputDiagnostic{
      .code = RenderInputDiagnosticCode::MissingPipelineFacts,
      .pass = StringID("Forward"),
      .debugId = StringID("diagnostic-input"),
      .message = "missing pipeline facts",
  });
  desc.stats.compilerInputCount = 2;
  desc.stats.acceptedInputCount = 1;
  desc.stats.rejectedInputCount = 1;
  desc.stats.submittedDrawCount = 3;
  desc.stats.submittedDispatchCount = 0;
  desc.stats.fallbackObservedCount = 1;

  EXPECT(desc.diagnostics.size() == 1, "desc should carry input diagnostics");
  EXPECT(desc.diagnostics.front().code ==
             RenderInputDiagnosticCode::MissingPipelineFacts,
         "desc diagnostic code should round-trip");
  EXPECT(desc.stats.compilerInputCount == 2,
         "desc stats should carry compiler input count");
  EXPECT(desc.stats.acceptedInputCount == 1,
         "desc stats should carry accepted compiler input count");
  EXPECT(desc.stats.rejectedInputCount == 1,
         "desc stats should carry rejected compiler input count");
  EXPECT(desc.stats.submittedDrawCount == 3,
         "desc stats should carry submitted draw count");
  EXPECT(desc.stats.submittedDispatchCount == 0,
         "desc stats should carry submitted dispatch count");
  EXPECT(desc.stats.fallbackObservedCount == 1,
         "desc stats should carry fallback observation count");
}

void testComputePayloadRemainsOnInput() {
  RenderComputeInput input;
  input.inputIndex = 7;
  input.groupCountX = 4;
  input.groupCountY = 5;
  input.groupCountZ = 6;
  input.readbackResource = StringID("readback.output");

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;

  EXPECT(input.kind() == RenderInputKind::Compute,
         "compute input should report compute kind");
  EXPECT(input.groupCountX == 4 && input.groupCountY == 5 &&
             input.groupCountZ == 6,
         "compute dispatch payload should remain on input");
  EXPECT(input.readbackResource.has_value(),
         "compute readback payload should remain on input");
  EXPECT(desc.accepted(), "accepted desc should indicate submit eligibility");
  EXPECT(desc.inputIndex == 7, "desc should reference compute input by index");
  EXPECT(!HasGroupCounts<RenderInputDesc>,
         "desc should not own compute dispatch payload");
  EXPECT(!HasReadbackResource<RenderInputDesc>,
         "desc should not own compute readback payload");
}

void testDescCarriesPipelineFacts() {
  RenderInputDesc desc;
  desc.pipelineKey = PipelineKey{StringID("pipeline.forward")};
  desc.shaderUri = StringID("shader://forward");
  desc.shaderVariantKey = StringID("variant.pbr");
  desc.reflectionIdentity = StringID("reflection.forward");
  desc.bindingPlan.descriptors.push_back(DescriptorResourceRef{});
  desc.resourceDependencies.push_back(GpuResourceRef{});

  EXPECT(desc.pipelineKey == PipelineKey{StringID("pipeline.forward")},
         "desc should carry pipeline key");
  EXPECT(desc.shaderUri == StringID("shader://forward"),
         "desc should carry shader uri");
  EXPECT(desc.shaderVariantKey == StringID("variant.pbr"),
         "desc should carry shader variant key");
  EXPECT(desc.reflectionIdentity == StringID("reflection.forward"),
         "desc should carry reflection identity");
  EXPECT(desc.bindingPlan.descriptors.size() == 1,
         "desc should carry descriptor binding plan");
  EXPECT(desc.resourceDependencies.size() == 1,
         "desc should carry resource dependencies");
}

void testFullscreenTriangleBuildsOneInputAndDesc() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("render_paths/Post/post_process");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);
  const auto descs =
      compiler.prepare(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);

  EXPECT(inputs.size() == 1, "fullscreen input should create one input");
  const auto *draw =
      dynamic_cast<const RenderDrawInput *>(inputs.front().get());
  EXPECT(draw != nullptr, "fullscreen input should be a draw input");
  EXPECT(draw->source == RenderDrawInputSource::FullscreenTriangle,
         "draw input should identify fullscreen triangle source");
  EXPECT(descs.size() == 1, "fullscreen input should prepare one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "fullscreen input without shader facts should be rejected");
    EXPECT(
        hasDiagnosticCode(descs.front(),
                          RenderInputDiagnosticCode::MissingShaderReflection),
        "fullscreen rejection should report missing shader reflection");
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MissingPipelineFacts),
           "fullscreen rejection should report missing pipeline facts");
  }
}

void testPrepareReferencesInputWithoutCopyingDrawCommands() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("render_paths/Post/post_process");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);
  const auto descs =
      compiler.prepare(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);

  const auto *draw =
      dynamic_cast<const RenderDrawInput *>(inputs.front().get());
  EXPECT(draw != nullptr, "fullscreen payload should remain typed draw input");
  if (draw != nullptr) {
    EXPECT(draw->drawCommands.size() == 1,
           "prepare should leave draw commands on input");
  }
  EXPECT(descs.size() == 1, "prepare should return one desc");
  if (!descs.empty()) {
    EXPECT(descs.front().inputIndex == 0,
           "desc should reference input by index");
  }
  EXPECT(!HasDrawCommands<RenderInputDesc>,
         "prepared desc should not copy draw commands");
}

void testFullscreenDescStatsAndSkeletonPipelineFactsAreRejected() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("render_paths/Post/post_process");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);
  const auto descs =
      compiler.prepare(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);

  EXPECT(descs.size() == 1, "fullscreen should prepare one desc");
  if (descs.empty()) {
    return;
  }

  const auto &desc = descs.front();
  EXPECT(desc.pipelineKey.id.id != 0,
         "fullscreen desc should carry pipeline key");
  EXPECT(desc.pipelineBuildDesc.key == desc.pipelineKey,
         "pipeline build desc key should match desc pipeline key");
  EXPECT(desc.shaderUri == StringID("render_paths/Post/post_process"),
         "fullscreen desc should carry shader uri");
  EXPECT(desc.stats.compilerInputCount == 1, "stats should count one input");
  EXPECT(!desc.accepted(),
         "fullscreen desc with skeleton shader facts should be rejected");
  EXPECT(hasFatalPipelineDiagnostic(desc),
         "fullscreen rejected desc should carry fatal pipeline diagnostic");
  EXPECT(desc.stats.acceptedInputCount == 0,
         "stats should count no accepted inputs");
  EXPECT(desc.stats.rejectedInputCount == 1,
         "stats should count one rejected input");
  EXPECT(desc.stats.submittedDrawCount == 0,
         "stats should not count rejected draw commands as submitted");
  EXPECT(desc.stats.submittedDispatchCount == 0,
         "stats should count no compute dispatches");
}

void testComputeInputWithoutShaderFactsIsRejected() {
  FramePass pass;
  pass.name = StringID("ComputeProbe");
  pass.stage = RenderPassStage::Compute;
  pass.dispatch = RenderPassDispatch::Compute;
  pass.input.kind = RenderPassInputKind::ComputeDispatch;
  pass.shaderUri = ResourceUri("compute_probe");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);
  const auto descs =
      compiler.prepare(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);

  EXPECT(inputs.size() == 1, "compute pass should build one input");
  EXPECT(descs.size() == 1, "compute pass should prepare one desc");
  if (descs.empty()) {
    return;
  }
  const auto &desc = descs.front();
  EXPECT(!desc.accepted(),
         "compute input without shader facts should be rejected");
  EXPECT(hasDiagnosticCode(desc,
                           RenderInputDiagnosticCode::MissingShaderReflection),
         "compute rejection should report missing shader reflection");
  EXPECT(
      hasDiagnosticCode(desc, RenderInputDiagnosticCode::MissingPipelineFacts),
      "compute rejection should report missing pipeline facts");
  EXPECT(desc.stats.acceptedInputCount == 0,
         "stats should count no accepted compute input");
  EXPECT(desc.stats.rejectedInputCount == 1,
         "stats should count rejected compute input");
  EXPECT(desc.stats.submittedDispatchCount == 0,
         "stats should not count rejected compute input as submitted");
}

void testFullscreenDescUsesPreparedPassFactsAndGraphReads() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("render_paths/Post/post_process");

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureBinding("SceneColor")},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 21}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 22}},
      });
  FrameGraphSampledResource sceneColor(StringID("scene.hdrColor"),
                                       StringID("SceneColor"));
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID("fullscreen.post_process.variant");
  passFacts.shaderProgram.shaderName = "render_paths/Post/post_process";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;
  passFacts.renderState.depthTestEnable = false;
  passFacts.descriptorResources.emplace_back(sceneColor);

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  Scene scene("FullscreenPreparedFactsScene");
  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1, "fullscreen prepared facts should produce desc");
  if (descs.empty()) {
    return;
  }
  const auto &desc = descs.front();
  expectAcceptedDescHasBackendPipelineFacts(
      desc, "fullscreen desc with prepared shader facts should be accepted");
  EXPECT(desc.pipelineBuildDesc.bindings.size() == 1,
         "fullscreen desc should carry shader reflection bindings");
  EXPECT(hasDescriptorBindingName(desc, StringID("SceneColor")),
         "fullscreen graph read should be in descriptor binding plan");
  EXPECT(hasResourceDependencyBindingName(desc, StringID("SceneColor")),
         "fullscreen graph read should be a resource dependency");
  EXPECT(desc.stats.acceptedInputCount == 1,
         "fullscreen stats should count accepted desc");
  EXPECT(desc.stats.submittedDrawCount == 1,
         "fullscreen stats should count one submitted draw command");
}

void testComputeDescUsesPreparedPassFacts() {
  FramePass pass;
  pass.name = StringID("ComputeProbe");
  pass.stage = RenderPassStage::Compute;
  pass.dispatch = RenderPassDispatch::Compute;
  pass.input.kind = RenderPassInputKind::ComputeDispatch;
  pass.shaderUri = ResourceUri("compute_probe");

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{
          makeComputeStorageBinding("ComputeOutput")},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Compute,
                          std::vector<u32>{0x07230203, 31}},
      });
  TestGpuResource output(ResourceType::StorageBuffer,
                         StringID("ComputeOutput"));
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID("compute.probe.variant");
  passFacts.shaderProgram.shaderName = "compute_probe";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;
  passFacts.descriptorResources.emplace_back(output);

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  Scene scene("ComputePreparedFactsScene");
  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1, "compute prepared facts should produce desc");
  if (descs.empty()) {
    return;
  }
  const auto &desc = descs.front();
  expectAcceptedDescHasBackendPipelineFacts(
      desc, "compute desc with prepared shader facts should be accepted");
  EXPECT(desc.pipelineBuildDesc.type == PipelineBuildType::Compute,
         "compute desc should carry compute pipeline build type");
  EXPECT(hasDescriptorBindingName(desc, StringID("ComputeOutput")),
         "compute descriptor should be in binding plan");
  EXPECT(desc.stats.acceptedInputCount == 1,
         "compute stats should count accepted input");
  EXPECT(desc.stats.submittedDispatchCount == 1,
         "compute stats should count accepted dispatch");
}

void testRenderWorkCompilerRejectsMetadataOnlyBakeSourcePayload() {
  FramePass pass;
  pass.name = StringID("CustomBakeSourcePass");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("render_paths/Bake/environment_to_cubemap");
  pass.reads.push_back(
      FrameGraphRead::sampled(StringID("bake.environment.cubemap"),
                              StringID("BakeEnvironmentCubemap")));

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{
          makeTextureCubeBinding("BakeEnvironmentCubemap")},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 41}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 42}},
      });
  FrameGraphSampledResource metadataOnlySource(
      StringID("bake.environment.cubemap"), StringID("BakeEnvironmentCubemap"));
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID("bake.environment.variant");
  passFacts.shaderProgram.shaderName =
      "render_paths/Bake/environment_to_cubemap";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;
  passFacts.descriptorResources.emplace_back(metadataOnlySource);

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  Scene scene("MetadataOnlyBakeSourceScene");
  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1, "bake source pass should produce one desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "metadata-only bake source payload should reject desc");
  EXPECT(hasDiagnosticCode(descs.front(),
                           RenderInputDiagnosticCode::MissingResource),
         "metadata-only bake source should report missing resource");
  EXPECT(hasDiagnosticMessage(descs.front(), "typed payload"),
         "metadata-only bake source diagnostic should require typed payload");
}

void testRenderWorkCompilerRejectsBakeComputePayloadWithoutReadback() {
  FramePass pass;
  pass.name = StringID("CustomBakeComputePayload");
  pass.stage = RenderPassStage::Compute;
  pass.dispatch = RenderPassDispatch::Compute;
  pass.input.kind = RenderPassInputKind::ComputeDispatch;
  pass.shaderUri = ResourceUri("render_paths/Bake/environment_diffuse_sh9");
  pass.payloads.push_back(RenderPathPayloadContract{
      .name = "diffuse_sh9",
      .target = "bake.environment.diffuse_sh9",
      .format = "SH9RgbFloat",
      .kind = "sh9",
  });

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Compute,
                          std::vector<u32>{0x07230203, 43}},
      });
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID("bake.compute.payload.variant");
  passFacts.shaderProgram.shaderName =
      "render_paths/Bake/environment_diffuse_sh9";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  Scene scene("BakeComputeMissingReadbackScene");
  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1, "bake compute pass should produce one desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "bake compute payload without readback should reject desc");
  EXPECT(hasDiagnosticCode(descs.front(),
                           RenderInputDiagnosticCode::MissingResource),
         "missing bake compute output should report missing resource");
  EXPECT(hasDiagnosticMessage(descs.front(), "readback"),
         "missing bake compute output diagnostic should name readback payload");
}

void testSceneRenderableValidatedShaderFactsPreparePipelineDesc() {
  auto renderable = std::make_shared<ValidatedRenderable>(Pass_Forward);
  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = Pass_Forward;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("validated_forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "validated renderable should produce one draw input");
  EXPECT(descs.size() == 1,
         "validated renderable should produce one prepared desc");
  if (inputs.empty() || descs.empty()) {
    return;
  }

  const auto *draw =
      dynamic_cast<const RenderDrawInput *>(inputs.front().get());
  EXPECT(draw != nullptr, "validated renderable input should be draw input");
  if (draw == nullptr) {
    return;
  }

  const auto &desc = descs.front();
  expectAcceptedDescHasBackendPipelineFacts(
      desc, "validated renderable desc should be accepted");
  EXPECT(desc.pipelineKey.id.id != 0,
         "accepted scene desc should carry pipeline key");
  EXPECT(desc.pipelineBuildDesc.key == desc.pipelineKey,
         "pipeline build desc key should match desc key");
  EXPECT(desc.shaderUri == StringID("validated_forward"),
         "desc should carry shader uri");
  EXPECT(desc.shaderVariantKey ==
             renderable->shaderProgram().getPipelineSignature(),
         "desc should carry shader program variant key");
  EXPECT(desc.reflectionIdentity.id != 0,
         "desc should carry shader reflection identity");
  EXPECT(desc.pipelineBuildDesc.stages.size() == renderable->stages().size(),
         "pipeline build desc should carry shader stages");
  EXPECT(desc.pipelineBuildDesc.bindings == renderable->bindings(),
         "pipeline build desc should carry reflection bindings");
  EXPECT(desc.pipelineBuildDesc.vertexLayout == renderable->vertexLayout(),
         "pipeline build desc should carry vertex layout");
  EXPECT(desc.pipelineBuildDesc.renderState.cullMode == CullMode::Front,
         "pipeline build desc should carry render state");
  EXPECT(desc.pipelineBuildDesc.topology == PrimitiveTopology::TriangleList,
         "pipeline build desc should carry topology");
  EXPECT(desc.resourceDependencies.size() == 2,
         "desc should carry vertex and index resource dependencies");
}

void testSceneRenderableIncludesCameraSceneResourceBinding() {
  auto renderable = std::make_shared<ValidatedRenderable>(
      Pass_Forward, "1", "validated.renderable.material.type",
      "camera_ubo_renderable",
      std::vector<ShaderResourceBinding>{makeUniformBinding("CameraUBO")});
  Scene scene("CompilerScene");
  scene.addRenderable(renderable);
  auto cameraNode = SceneNode::create("compiler_camera");
  cameraNode->addComponent<CameraComponent>();
  scene.addCamera(cameraNode);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = Pass_Forward;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("validated_forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(!inputs.empty(),
         "active camera scene should still produce draw inputs");
  EXPECT(!descs.empty(), "active camera scene should prepare descs");
  auto descIt = std::find_if(
      descs.begin(), descs.end(), [](const RenderInputDesc &candidate) {
        return candidate.debugId == StringID("debug.camera_ubo_renderable");
      });
  EXPECT(descIt != descs.end(),
         "active camera scene should prepare the target renderable desc");
  if (descIt == descs.end()) {
    return;
  }

  const auto &desc = *descIt;
  expectAcceptedDescHasBackendPipelineFacts(
      desc, "CameraUBO renderable desc should be accepted");
  EXPECT(hasDescriptorBindingName(desc, StringID("CameraUBO")),
         "accepted desc binding plan should include CameraUBO");
  EXPECT(hasResourceDependencyBindingName(desc, StringID("CameraUBO")),
         "accepted desc dependencies should include CameraUBO");
}

void testSceneRenderableRejectsUnresolvedRequiredBinding() {
  auto renderable = std::make_shared<ValidatedRenderable>(
      Pass_Forward, "1", "validated.renderable.material.type",
      "unresolved_binding_renderable",
      std::vector<ShaderResourceBinding>{
          makeUniformBinding("UnresolvedRequiredBinding")});
  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = Pass_Forward;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("validated_forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "unresolved binding renderable should produce one input");
  EXPECT(descs.size() == 1,
         "unresolved binding renderable should produce one desc");
  if (descs.empty()) {
    return;
  }

  const auto &desc = descs.front();
  EXPECT(!desc.accepted(),
         "desc with unresolved reflected binding should be rejected");
  EXPECT(
      hasDiagnosticCode(desc, RenderInputDiagnosticCode::MissingBinding) ||
          hasDiagnosticCode(desc, RenderInputDiagnosticCode::MissingResource),
      "unresolved binding rejection should report binding/resource diagnostic");
}

void testSceneRenderablePipelineKeyUsesMaterialVariantNotTypeSignature() {
  const std::string sharedMaterialType = "shared.validated.material.type";
  auto first = std::make_shared<ValidatedRenderable>(
      Pass_Forward, "variant-a", sharedMaterialType, "variant_a_renderable");
  auto second = std::make_shared<ValidatedRenderable>(
      Pass_Forward, "variant-b", sharedMaterialType, "variant_b_renderable");

  Scene scene("CompilerScene");
  scene.addRenderable(first);
  scene.addRenderable(second);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = Pass_Forward;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.material.required = false;
  pass.input.material.types = {sharedMaterialType};
  pass.shaderUri = ResourceUri("validated_forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 2,
         "same material type variants should both produce draw inputs");
  EXPECT(descs.size() == 2,
         "same material type variants should both produce descs");
  if (descs.size() != 2) {
    return;
  }

  expectAcceptedDescHasBackendPipelineFacts(
      descs[0], "first material variant desc should be accepted");
  expectAcceptedDescHasBackendPipelineFacts(
      descs[1], "second material variant desc should be accepted");
  EXPECT(descs[0].shaderVariantKey != descs[1].shaderVariantKey,
         "fixture should produce distinct shader/material variants");
  EXPECT(descs[0].pipelineKey != descs[1].pipelineKey,
         "pipeline key must separate shader/material variants even when "
         "material type signature matches");
}

void testPipelineKeyIncludesGenericPassSpecializationValue() {
  const StringID materialVariant("validated.pass.feature.variant");
  const StringID renderPathNode("validated.pass.feature.node");
  constexpr u32 kPassFeatureConstantId = 17;

  auto makeDesc = [&](std::vector<ShaderSpecializationConstant> constants) {
    const PipelineKey key =
        PipelineKey::build(materialVariant, renderPathNode, constants);
    return PipelineBuildDesc::graphics(key, materialVariant, RenderTargetDesc{},
                                       {}, {}, VertexLayout{}, RenderState{},
                                       PrimitiveTopology::TriangleList,
                                       std::nullopt, {}, std::move(constants));
  };

  std::vector<ShaderSpecializationConstant> disabledConstants = {
      ShaderSpecializationConstant{
          .constantId = kPassFeatureConstantId,
          .stage = ShaderStage::Fragment,
          .type = ShaderSpecializationValueType::Bool,
          .valueU32 = 0,
      },
  };
  std::vector<ShaderSpecializationConstant> enabledConstants =
      disabledConstants;
  enabledConstants.front().valueU32 = 1;

  PipelineBuildDesc disabledDesc = makeDesc(disabledConstants);
  PipelineBuildDesc enabledDesc = makeDesc(enabledConstants);

  EXPECT(disabledDesc.specializationConstants == disabledConstants,
         "pipeline desc should carry pass specialization facts");
  EXPECT(enabledDesc.specializationConstants == enabledConstants,
         "pipeline desc should carry changed pass specialization facts");
  EXPECT(disabledDesc.key != enabledDesc.key,
         "pipeline key must include reflected pass specialization values");

  std::vector<ShaderSpecializationConstant> sortedConstants = {
      ShaderSpecializationConstant{
          .constantId = 11,
          .stage = ShaderStage::Vertex,
          .type = ShaderSpecializationValueType::UInt,
          .valueU32 = 7,
      },
      ShaderSpecializationConstant{
          .constantId = 23,
          .stage = ShaderStage::Fragment,
          .type = ShaderSpecializationValueType::Bool,
          .valueU32 = 1,
      },
  };
  std::vector<ShaderSpecializationConstant> reversedConstants = {
      sortedConstants[1], sortedConstants[0]};
  EXPECT(PipelineKey::build(materialVariant, renderPathNode, sortedConstants) ==
             PipelineKey::build(materialVariant, renderPathNode,
                                reversedConstants),
         "pipeline key specialization signature should be order-stable");
}

RenderFeature makeCompilerPassFeature() {
  RenderFeature feature;
  feature.name = "CompilerPassFeature";
  feature.feature = "compilerPassFeature";
  feature.level = RenderFeatureLevel::Pass;
  feature.shader = RenderFeatureShaderContract{
      .uri = ResourceUri("memory://shaders/compiler_pass_specialization"),
  };
  feature.parameters["render_probe"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "true",
  };
  feature.parameters["enable_probe_debug"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
  };
  return feature;
}

RenderFeature makeCompilerPassFeatureWithVolatileRuntimeField() {
  RenderFeature feature = makeCompilerPassFeature();
  feature.parameters["enableIblLighting"] = RenderFeatureParameter{
      .kind = "bool",
      .binding = "PassRuntimeUBO",
      .member = "enableIblLighting",
      .required = true,
      .volatileRuntime = true,
  };
  return feature;
}

RenderFeature makeCompilerSurfaceLightingFeature() {
  RenderFeature feature;
  feature.name = "SurfaceLighting";
  feature.feature = "surfaceLighting";
  feature.level = RenderFeatureLevel::Shader;
  feature.shader = RenderFeatureShaderContract{
      .uri = ResourceUri("features/surface_lighting"),
  };
  feature.parameters["enableIblLighting"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "true",
      .binding = "SurfaceLightingUBO",
      .member = "enableIblLighting",
      .required = true,
  };
  feature.parameters["diffuseIblIntensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "SurfaceLightingUBO",
      .member = "diffuseIblIntensity",
      .required = true,
  };
  feature.parameters["specularIblIntensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "SurfaceLightingUBO",
      .member = "specularIblIntensity",
      .required = true,
  };
  feature.parameters["environmentIblReady"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
      .binding = "SurfaceLightingUBO",
      .member = "environmentIblReady",
      .required = true,
  };
  feature.parameters["standardPbrIblReady"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
      .binding = "SurfaceLightingUBO",
      .member = "standardPbrIblReady",
      .required = true,
  };
  return feature;
}

std::vector<ShaderResourceBinding> makeSurfaceLightingBindings() {
  return {
      ShaderResourceBinding{
          .name = "SurfaceLightingUBO",
          .set = 4,
          .binding = 2,
          .type = ShaderPropertyType::UniformBuffer,
          .members = {StructMemberInfo{"enableIblLighting",
                                       ShaderPropertyType::Int, 0, 4},
                      StructMemberInfo{"diffuseIblIntensity",
                                       ShaderPropertyType::Float, 4, 4},
                      StructMemberInfo{"specularIblIntensity",
                                       ShaderPropertyType::Float, 8, 4},
                      StructMemberInfo{"environmentIblReady",
                                       ShaderPropertyType::Int, 12, 4},
                      StructMemberInfo{"standardPbrIblReady",
                                       ShaderPropertyType::Int, 16, 4}},
      },
  };
}

IShaderSharedPtr makeSurfaceLightingShader(u32 programHashSeed) {
  return std::make_shared<FakeShader>(
      makeSurfaceLightingBindings(),
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, programHashSeed}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, programHashSeed + 1}},
      });
}

FramePass makeSurfaceLightingFullscreenPass(const char *name,
                                            ResourceUri shaderUri) {
  FramePass pass;
  pass.name = StringID(name);
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = std::move(shaderUri);
  pass.reads.push_back(
      FrameGraphRead::sampled(StringID("feature.surfaceLighting"), StringID{}));
  return pass;
}

void addSurfaceLightingBakeFacts(FramePass &pass, bool environmentBake,
                                 bool materialIblBake) {
  if (environmentBake) {
    pass.reads.push_back(
        FrameGraphRead::sampled(StringID("scene.environmentBake"), StringID{}));
  }
  if (materialIblBake) {
    pass.reads.push_back(
        FrameGraphRead::sampled(StringID("scene.materialIblBake"), StringID{}));
  }
}

RenderWorkBuildContext::PassPreparationFacts
makeSurfaceLightingPassFacts(const FramePass &pass,
                             const IShaderSharedPtr &shader) {
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID(
      "surface.lighting." + GlobalStringTable::get().toDebugString(pass.name));
  passFacts.shaderProgram.shaderName = pass.shaderUri.string();
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;
  return passFacts;
}

void testRenderWorkCompilerResolvesPassFeatureSpecializationFromReflection() {
  const ResourceUri shaderUri("memory://shaders/compiler_pass_specialization");
  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 51}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 52}},
      },
      std::vector<VertexInputAttribute>{VertexInputAttribute{
          .name = "inPos", .location = 0, .type = DataType::Float3}},
      std::vector<ShaderSpecializationConstantInfo>{
          ShaderSpecializationConstantInfo{
              .name = "render_probe",
              .stage = ShaderStage::Fragment,
              .constantId = 17,
              .type = ShaderSpecializationValueType::Bool,
          },
          ShaderSpecializationConstantInfo{
              .name = "enable_probe_debug",
              .stage = ShaderStage::Vertex,
              .constantId = 23,
              .type = ShaderSpecializationValueType::Bool,
          },
      });

  Scene scene("PassFeatureSpecializationCompilerScene");
  [[maybe_unused]] const ShaderHandle shaderHandle =
      scene.resources().registerShaderResource(
          shaderUri, std::vector<ResourceUri>{shaderUri}, shader);
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/compiler_pass_feature"),
          makeCompilerPassFeature());

  RenderPathGraph graph;
  graph.name = "CompilerPassFeatureGraph";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "compilerPassFeature",
      .uri = ResourceUri("memory://features/compiler_pass_feature"),
  });
  RenderPassNode node;
  node.id = "CompilerPass";
  node.shaderUri = shaderUri;
  node.stage = RenderPassStage::Raster;
  node.dispatch = RenderPassDispatch::Fullscreen;
  node.input.kind = RenderPassInputKind::FullscreenTriangle;
  node.sources = {"feature.compilerPassFeature"};
  graph.passes.push_back(node);
  [[maybe_unused]] const RenderPathGraphHandle graphHandle =
      scene.resources().registerRenderPathGraph(
          ResourceUri("memory://graphs/compiler_pass_feature"), graph);

  FramePass pass;
  pass.name = StringID("CompilerPass");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = shaderUri;
  pass.reads.push_back(FrameGraphRead::sampled(
      StringID("feature.compilerPassFeature"), StringID{}));

  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID("compiler.pass.feature.variant");
  passFacts.shaderProgram.shaderName = shaderUri.string();
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "pass-level feature fullscreen pass should produce one desc");
  if (descs.empty()) {
    return;
  }

  const auto &constants =
      descs.front().pipelineBuildDesc.specializationConstants;
  EXPECT(
      constants.size() == 2,
      "pipeline desc should contain exactly reflected pass feature constants");
  const auto hasConstant = [&](u32 constantId, ShaderStage stage, u32 value) {
    return std::any_of(constants.begin(), constants.end(),
                       [&](const ShaderSpecializationConstant &constant) {
                         return constant.constantId == constantId &&
                                constant.stage == stage &&
                                constant.type ==
                                    ShaderSpecializationValueType::Bool &&
                                constant.valueU32 == value;
                       });
  };
  EXPECT(hasConstant(17, ShaderStage::Fragment, 1),
         "render_probe should use reflected constant id 17 and YAML true");
  EXPECT(
      hasConstant(23, ShaderStage::Vertex, 0),
      "enable_probe_debug should use reflected constant id 23 and YAML false");
  EXPECT(std::none_of(constants.begin(), constants.end(),
                      [](const ShaderSpecializationConstant &constant) {
                        return constant.constantId < 10;
                      }),
         "compiler should not add unrelated hardcoded Forward constants");
}

void testRenderWorkCompilerExcludesVolatilePassFieldsFromSpecialization() {
  const ResourceUri shaderUri("memory://shaders/compiler_pass_specialization");
  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{
          ShaderResourceBinding{
              .name = "PassRuntimeUBO",
              .set = 4,
              .binding = 1,
              .type = ShaderPropertyType::UniformBuffer,
              .members = {StructMemberInfo{"enableIblLighting",
                                           ShaderPropertyType::Int, 0, 4}},
          },
      },
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 51}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 52}},
      },
      std::vector<VertexInputAttribute>{VertexInputAttribute{
          .name = "inPos", .location = 0, .type = DataType::Float3}},
      std::vector<ShaderSpecializationConstantInfo>{
          ShaderSpecializationConstantInfo{
              .name = "render_probe",
              .stage = ShaderStage::Fragment,
              .constantId = 17,
              .type = ShaderSpecializationValueType::Bool,
          },
          ShaderSpecializationConstantInfo{
              .name = "enable_probe_debug",
              .stage = ShaderStage::Vertex,
              .constantId = 23,
              .type = ShaderSpecializationValueType::Bool,
          },
      });

  Scene scene("PassFeatureVolatileCompilerScene");
  [[maybe_unused]] const ShaderHandle shaderHandle =
      scene.resources().registerShaderResource(
          shaderUri, std::vector<ResourceUri>{shaderUri}, shader);
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/compiler_pass_feature"),
          makeCompilerPassFeatureWithVolatileRuntimeField());

  RenderPathGraph graph;
  graph.name = "CompilerPassFeatureVolatileGraph";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "compilerPassFeature",
      .uri = ResourceUri("memory://features/compiler_pass_feature"),
  });
  RenderPassNode node;
  node.id = "CompilerPass";
  node.shaderUri = shaderUri;
  node.stage = RenderPassStage::Raster;
  node.dispatch = RenderPassDispatch::Fullscreen;
  node.input.kind = RenderPassInputKind::FullscreenTriangle;
  node.sources = {"feature.compilerPassFeature"};
  graph.passes.push_back(node);
  [[maybe_unused]] const RenderPathGraphHandle graphHandle =
      scene.resources().registerRenderPathGraph(
          ResourceUri("memory://graphs/compiler_pass_feature_volatile"), graph);

  FramePass pass;
  pass.name = StringID("CompilerPass");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = shaderUri;
  pass.reads.push_back(FrameGraphRead::sampled(
      StringID("feature.compilerPassFeature"), StringID{}));

  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = pass.name;
  passFacts.pipelineVariantKey = StringID("compiler.pass.feature.variant");
  passFacts.shaderProgram.shaderName = shaderUri.string();
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "volatile pass-level feature fullscreen pass should produce one desc");
  if (descs.empty()) {
    return;
  }

  const auto &constants =
      descs.front().pipelineBuildDesc.specializationConstants;
  const auto hasSpecializationConstant = [&](const std::string &name) -> bool {
    const PassFeatureData *data =
        scene.resources().findPassFeatureDataByFeatureName(
            "compilerPassFeature");
    return data != nullptr &&
           std::any_of(data->specializationValues.begin(),
                       data->specializationValues.end(),
                       [&](const PassFeatureSpecializationValue &value) {
                         return value.parameterName == name;
                       });
  };

  EXPECT(constants.size() == 2,
         "volatile pass field must not become a pipeline specialization "
         "constant");
  EXPECT(!hasSpecializationConstant("enableIblLighting"),
         "volatile IBL field must not become specialization constant");
}

void testRenderWorkCompilerSharesSurfaceLightingPayloadAcrossForwardAndDeferred() {
  const ResourceUri featureUri("memory://features/surface_lighting");
  const ResourceUri forwardShaderUri("memory://shaders/forward_surface");
  const ResourceUri deferredShaderUri("memory://shaders/deferred_lighting");
  auto forwardShader = makeSurfaceLightingShader(61);
  auto deferredShader = makeSurfaceLightingShader(71);

  Scene scene("SurfaceLightingSharedPayloadCompilerScene");
  [[maybe_unused]] const ShaderHandle forwardShaderHandle =
      scene.resources().registerShaderResource(
          forwardShaderUri, std::vector<ResourceUri>{forwardShaderUri},
          forwardShader);
  [[maybe_unused]] const ShaderHandle deferredShaderHandle =
      scene.resources().registerShaderResource(
          deferredShaderUri, std::vector<ResourceUri>{deferredShaderUri},
          deferredShader);
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          featureUri, makeCompilerSurfaceLightingFeature());

  RenderPathGraph graph;
  graph.name = "SurfaceLightingSharedPayloadGraph";
  graph.renderPath = RenderPath::Forward;
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "surfaceLighting",
      .uri = featureUri,
  });
  RenderPassNode forwardNode;
  forwardNode.id = "Forward";
  forwardNode.shaderUri = forwardShaderUri;
  forwardNode.stage = RenderPassStage::Raster;
  forwardNode.dispatch = RenderPassDispatch::Fullscreen;
  forwardNode.input.kind = RenderPassInputKind::FullscreenTriangle;
  forwardNode.sources = {"feature.surfaceLighting", "scene.environmentBake",
                         "scene.materialIblBake"};
  graph.passes.push_back(forwardNode);
  RenderPassNode deferredNode = forwardNode;
  deferredNode.id = "DeferredLighting";
  deferredNode.shaderUri = deferredShaderUri;
  graph.passes.push_back(deferredNode);

  const ResourceUri graphUri("memory://graphs/surface_lighting_shared");
  const RenderPathGraphHandle graphHandle =
      scene.resources().registerRenderPathGraph(graphUri, graph);
  EXPECT(graphHandle.isValid(),
         "graph with one surfaceLighting feature payload should register");
  if (graphHandle.isValid()) {
    const ResourceMetadata &metadata = scene.resources().metadata(graphHandle);
    EXPECT(std::count(metadata.dependencies.begin(),
                      metadata.dependencies.end(), featureUri) == 1,
           "graph metadata should depend on the shared surfaceLighting payload "
           "once");
  }

  FramePass forwardPass =
      makeSurfaceLightingFullscreenPass("Forward", forwardShaderUri);
  FramePass deferredPass =
      makeSurfaceLightingFullscreenPass("DeferredLighting", deferredShaderUri);
  addSurfaceLightingBakeFacts(forwardPass, true, true);
  addSurfaceLightingBakeFacts(deferredPass, true, true);

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(
      makeSurfaceLightingPassFacts(forwardPass, forwardShader));
  options.passPreparationFacts.push_back(
      makeSurfaceLightingPassFacts(deferredPass, deferredShader));

  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  RenderWorkCompiler compiler;

  const auto compilePass = [&](const FramePass &pass) {
    std::vector<std::unique_ptr<RenderInput>> inputs;
    compiler.buildInputs(pass, context, inputs);
    return compiler.prepare(pass, context, inputs);
  };

  const auto forwardDescs = compilePass(forwardPass);
  const auto deferredDescs = compilePass(deferredPass);
  EXPECT(forwardDescs.size() == 1,
         "Forward pass should produce one desc with surfaceLighting");
  EXPECT(deferredDescs.size() == 1,
         "DeferredLighting pass should produce one desc with surfaceLighting");
  if (forwardDescs.empty() || deferredDescs.empty()) {
    return;
  }

  const auto expectSurfaceLightingDesc = [](const RenderInputDesc &desc,
                                            const char *passName) {
    EXPECT(desc.accepted(),
           std::string(passName) +
               " should accept the shared surfaceLighting descriptor");
    EXPECT(hasDescriptorBindingName(desc, StringID("SurfaceLightingUBO")),
           std::string(passName) + " should bind the live surfaceLighting UBO");
    EXPECT(
        hasResourceDependencyBindingName(desc, StringID("SurfaceLightingUBO")),
        std::string(passName) +
            " should carry the surfaceLighting UBO as a resource "
            "dependency");
    EXPECT(desc.pipelineBuildDesc.specializationConstants.empty(),
           std::string(passName) +
               " should not turn surfaceLighting UBO facts into "
               "specialization constants");
  };

  expectSurfaceLightingDesc(forwardDescs.front(), "Forward");
  expectSurfaceLightingDesc(deferredDescs.front(), "DeferredLighting");
}

void testRenderWorkCompilerRejectsSurfaceLightingUboWithoutFeatureRead() {
  const ResourceUri featureUri("memory://features/surface_lighting");
  const ResourceUri shaderUri("memory://shaders/surface_without_read");
  auto shader = makeSurfaceLightingShader(81);

  Scene scene("SurfaceLightingMissingReadCompilerScene");
  [[maybe_unused]] const ShaderHandle shaderHandle =
      scene.resources().registerShaderResource(
          shaderUri, std::vector<ResourceUri>{shaderUri}, shader);
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          featureUri, makeCompilerSurfaceLightingFeature());

  FramePass pass;
  pass.name = StringID("Forward");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = shaderUri;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(
      makeSurfaceLightingPassFacts(pass, shader));

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "surfaceLighting missing-read pass should produce one desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "SurfaceLightingUBO reflection should require "
         "feature.surfaceLighting read");
  EXPECT(hasDiagnosticMessage(descs.front(), "feature.surfaceLighting"),
         "diagnostic should name missing feature.surfaceLighting read");
}

void testRenderWorkCompilerRejectsIblSurfaceLightingWithoutEnvironmentBake() {
  const ResourceUri featureUri("memory://features/surface_lighting");
  const ResourceUri shaderUri("memory://shaders/surface_without_environment");
  auto shader = makeSurfaceLightingShader(91);

  Scene scene("SurfaceLightingMissingEnvironmentBakeScene");
  [[maybe_unused]] const ShaderHandle shaderHandle =
      scene.resources().registerShaderResource(
          shaderUri, std::vector<ResourceUri>{shaderUri}, shader);
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          featureUri, makeCompilerSurfaceLightingFeature());

  FramePass pass = makeSurfaceLightingFullscreenPass("Forward", shaderUri);
  addSurfaceLightingBakeFacts(pass, false, true);

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(
      makeSurfaceLightingPassFacts(pass, shader));

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "IBL-enabled surfaceLighting pass should produce one desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "IBL-enabled surfaceLighting should require scene.environmentBake");
  EXPECT(hasDiagnosticCode(descs.front(),
                           RenderInputDiagnosticCode::MissingResource),
         "missing environment bake fact should report missing resource");
  EXPECT(hasDiagnosticMessage(descs.front(), "scene.environmentBake"),
         "diagnostic should name missing scene.environmentBake");
}

void testRenderWorkCompilerRejectsIblSurfaceLightingWithoutMaterialIblBake() {
  const ResourceUri featureUri("memory://features/surface_lighting");
  const ResourceUri shaderUri("memory://shaders/surface_without_material_ibl");
  auto shader = makeSurfaceLightingShader(101);

  Scene scene("SurfaceLightingMissingMaterialBakeScene");
  [[maybe_unused]] const ShaderHandle shaderHandle =
      scene.resources().registerShaderResource(
          shaderUri, std::vector<ResourceUri>{shaderUri}, shader);
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          featureUri, makeCompilerSurfaceLightingFeature());

  FramePass pass = makeSurfaceLightingFullscreenPass("Forward", shaderUri);
  addSurfaceLightingBakeFacts(pass, true, false);

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(
      makeSurfaceLightingPassFacts(pass, shader));

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "IBL-enabled surfaceLighting pass should produce one desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "IBL-enabled surfaceLighting should require scene.materialIblBake");
  EXPECT(hasDiagnosticCode(descs.front(),
                           RenderInputDiagnosticCode::MissingResource),
         "missing material IBL bake fact should report missing resource");
  EXPECT(hasDiagnosticMessage(descs.front(), "scene.materialIblBake"),
         "diagnostic should name missing scene.materialIblBake");
}

void testSceneRenderableMissingRequiredMaterialProducesRejectedDesc() {
  auto renderable =
      std::make_shared<MateriallessRenderable>("materialless_node");
  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = StringID("Forward");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.material.required = true;
  pass.shaderUri = ResourceUri("forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "selected scene renderable should produce one input");
  EXPECT(descs.size() == 1,
         "selected scene renderable should produce one desc");
  if (descs.empty()) {
    return;
  }

  EXPECT(!descs.front().accepted(),
         "materialless required input should be rejected");
  EXPECT(!descs.front().diagnostics.empty(),
         "materialless rejection should carry diagnostic");
  if (!descs.front().diagnostics.empty()) {
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MaterialRequired),
           "diagnostic should report missing required material");
  }
}

void testSceneRenderableMissingMaterialDoesNotUseSupportsPassAsSelection() {
  auto renderable = std::make_shared<MateriallessRenderable>(
      "materialless_without_pass", false);
  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = StringID("Forward");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.material.required = true;
  pass.shaderUri = ResourceUri("forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "missing material should be validated in prepare, not skipped by "
         "supportsPass");
  EXPECT(descs.size() == 1, "missing material should produce a rejected desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "missing required material should be rejected");
  EXPECT(!descs.front().diagnostics.empty(),
         "missing required material should carry diagnostic");
  if (!descs.front().diagnostics.empty()) {
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MaterialRequired),
           "diagnostic should be MaterialRequired");
  }
}

void testNoMaterialDebugRenderableAcceptedWithDrawPayload() {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  auto mesh = Mesh::create(vb, ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});
  auto node = SceneNode::create("debug_no_material");
  node->addComponent<MeshComponent>(mesh);
  node->setDebugOnlyRenderable(true);
  node->setRenderType(StringID("debug"));

  auto scene = Scene::create(nullptr);
  scene->addRenderable(node);
  EXPECT(node->getIndexBuffer().isValid(),
         "debug no-material SceneNode should expose an index buffer");

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = Pass_DebugOverlay;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.object.renderClasses = {"debug"};
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("render_paths/Debug/debug_overlay");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(*scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(*scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "debug no-material renderable should produce one input");
  const auto *draw =
      inputs.empty()
          ? nullptr
          : dynamic_cast<const RenderDrawInput *>(inputs.front().get());
  EXPECT(draw != nullptr, "debug no-material input should be draw input");
  if (draw != nullptr) {
    EXPECT(draw->drawCommands.size() == 1,
           "debug no-material draw payload should stay on input");
    if (!draw->drawCommands.empty()) {
      EXPECT(draw->drawCommands.front().indexCount == 3,
             "debug no-material draw command should use index buffer count");
    }
  }
  EXPECT(descs.size() == 1,
         "debug no-material renderable should prepare one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "debug no-material renderable without shader facts should reject");
    EXPECT(
        hasFatalPipelineDiagnostic(descs.front()),
        "debug no-material rejection should carry fatal pipeline diagnostic");
  }
  EXPECT(!HasDrawCommands<RenderInputDesc>,
         "debug no-material desc should not copy draw commands");
}

void testDebugObjectClassDoesNotRequireDebugOverlayPassName() {
  auto renderable = std::make_shared<MateriallessRenderable>("custom_debug");
  renderable->setIndexedTriangle();
  renderable->setDebugOnly(true);
  renderable->setRenderType(StringID("debug"));

  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = StringID("CustomDebugPass");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.object.renderClasses = {"debug"};
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("custom_debug");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "debug object class should not require Pass_DebugOverlay name");
  EXPECT(descs.size() == 1, "debug object class should still produce one desc");
  if (!descs.empty()) {
    EXPECT(
        !descs.front().accepted(),
        "debug object without shader facts should reject even for custom pass");
    EXPECT(hasFatalPipelineDiagnostic(descs.front()),
           "debug object rejection should carry fatal pipeline diagnostic");
  }
}

void testDebugMeshObjectClassAcceptsNoMaterialDebugRenderable() {
  auto renderable =
      std::make_shared<MateriallessRenderable>("debug_mesh_default_type");
  renderable->setIndexedTriangle();
  renderable->setDebugOnly(true);

  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = StringID("CustomDebugMeshPass");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.object.renderClasses = {"debug.mesh"};
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("custom_debug_mesh");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "debug.mesh object class should select debug-only renderable");
  const auto *draw =
      inputs.empty()
          ? nullptr
          : dynamic_cast<const RenderDrawInput *>(inputs.front().get());
  EXPECT(draw != nullptr, "debug.mesh selected input should be draw input");
  if (draw != nullptr) {
    EXPECT(draw->drawCommands.size() == 1,
           "debug.mesh no-material draw payload should stay on input");
  }
  EXPECT(descs.size() == 1,
         "debug.mesh no-material renderable should prepare one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "debug.mesh without shader facts should reject");
    EXPECT(hasFatalPipelineDiagnostic(descs.front()),
           "debug.mesh rejection should carry fatal pipeline diagnostic");
  }
  EXPECT(!HasDrawCommands<RenderInputDesc>,
         "debug.mesh desc should not copy draw commands");
}

void testShadowPassNameDoesNotOverrideZeroVisibleMask() {
  auto shadowRenderable =
      std::make_shared<MateriallessRenderable>("shadow_visibility");
  shadowRenderable->setIndexedTriangle();
  shadowRenderable->setRenderType(StringID("debug"));

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = 0u;

  auto compileForPass = [&](StringID passName) {
    Scene scene("CompilerScene");
    scene.addRenderable(shadowRenderable);

    FramePass pass;
    pass.name = passName;
    pass.stage = RenderPassStage::Raster;
    pass.dispatch = RenderPassDispatch::Draw;
    pass.input.kind = RenderPassInputKind::SceneRenderables;
    pass.input.object.renderClasses = {"debug"};
    pass.input.material.required = false;
    pass.shaderUri = ResourceUri("shadow_visibility");

    RenderWorkCompiler compiler;
    std::vector<std::unique_ptr<RenderInput>> inputs;
    compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                         inputs);
    return inputs.size();
  };

  const usize customInputCount = compileForPass(StringID("CustomShadowLike"));
  const usize shadowInputCount = compileForPass(Pass_Shadow);

  EXPECT(customInputCount == 0,
         "explicit zero visibleMask should reject custom pass input");
  EXPECT(shadowInputCount == customInputCount,
         "Pass_Shadow name should not override explicit zero visibleMask");
}

void testPassNameDoesNotCreateDefaultVisibilityWithoutCamera() {
  auto noCameraRenderable =
      std::make_shared<MateriallessRenderable>("no_camera_visibility");
  noCameraRenderable->setIndexedTriangle();
  noCameraRenderable->setDebugOnly(true);

  auto compileForPass = [&](StringID passName) {
    Scene scene("CompilerScene");
    scene.addRenderable(noCameraRenderable);

    FramePass pass;
    pass.name = passName;
    pass.stage = RenderPassStage::Raster;
    pass.dispatch = RenderPassDispatch::Draw;
    pass.input.kind = RenderPassInputKind::SceneRenderables;
    pass.input.object.renderClasses = {"debug"};
    pass.input.material.required = false;
    pass.shaderUri = ResourceUri("default_visibility");

    RenderWorkCompiler compiler;
    std::vector<std::unique_ptr<RenderInput>> inputs;
    compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene), inputs);
    return inputs.size();
  };

  const usize customInputCount = compileForPass(StringID("CustomNoCameraPass"));
  const usize shadowInputCount = compileForPass(Pass_Shadow);

  EXPECT(customInputCount == 0,
         "scene without active matching camera should not select by default");
  EXPECT(shadowInputCount == customInputCount,
         "Pass_Shadow name should not create default visibility");
}

void testUnsupportedObjectClassProducesRejectedDesc() {
  auto renderable =
      std::make_shared<MateriallessRenderable>("typed_debug_renderable");
  renderable->setIndexedTriangle();
  renderable->setRenderType(StringID("debug"));

  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = StringID("Forward");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.object.renderClasses = {"unsupported-type"};
  pass.input.material.required = false;
  pass.shaderUri = ResourceUri("forward");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "object filter mismatch should remain visible as an input");
  EXPECT(descs.size() == 1, "object filter mismatch should produce desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "unsupported object class should not be accepted");
  EXPECT(!descs.front().diagnostics.empty(),
         "unsupported object class should carry diagnostic");
  if (!descs.front().diagnostics.empty()) {
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::ObjectClassRejected),
           "diagnostic should be ObjectClassRejected");
  }
}

void testMaterialTypeFilterRejectsNoMaterialRenderable() {
  auto renderable = std::make_shared<MateriallessRenderable>("debug_untyped");
  renderable->setIndexedTriangle();
  renderable->setDebugOnly(true);
  renderable->setRenderType(StringID("debug"));

  Scene scene("CompilerScene");
  scene.addRenderable(renderable);

  RenderWorkBuildContext::RealtimeOptions options;
  options.visibleMask = VisibilityMask_All;

  FramePass pass;
  pass.name = Pass_DebugOverlay;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.input.kind = RenderPassInputKind::SceneRenderables;
  pass.input.object.renderClasses = {"debug"};
  pass.input.material.required = false;
  pass.input.material.types = {"matte"};
  pass.shaderUri = ResourceUri("render_paths/Debug/debug_overlay");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtime(scene, options),
                       inputs);
  const auto descs = compiler.prepare(
      pass, RenderWorkBuildContext::realtime(scene, options), inputs);

  EXPECT(inputs.size() == 1,
         "material type mismatch should remain visible as an input");
  EXPECT(descs.size() == 1, "material type mismatch should produce desc");
  if (descs.empty()) {
    return;
  }
  EXPECT(!descs.front().accepted(),
         "material type filter should reject materialless renderable");
  EXPECT(!descs.front().diagnostics.empty(),
         "material type filter should carry diagnostic");
  if (!descs.front().diagnostics.empty()) {
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MaterialTypeRejected),
           "diagnostic should be MaterialTypeRejected");
  }
}

RenderFeature makeCompilerEnvironmentFeature(bool includeColor = true) {
  RenderFeature feature;
  feature.name = "EnvironmentLighting";
  feature.feature = "environmentLighting";
  feature.parameters["environmentMap"] = RenderFeatureParameter{
      .kind = "textureCube",
      .uri = ResourceUri("builtin:env/white_cube"),
      .valueType = "linear-radiance",
      .binding = "SkyboxMap",
      .required = true,
  };
  if (includeColor) {
    feature.parameters["color"] = RenderFeatureParameter{
        .kind = "vec3",
        .value = "[1.0, 1.0, 1.0]",
        .binding = "EnvironmentLightingUBO",
        .member = "color",
        .required = true,
    };
  }
  feature.parameters["intensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "EnvironmentLightingUBO",
      .member = "intensity",
      .required = true,
  };
  feature.parameters["rotation"] = RenderFeatureParameter{
      .kind = "float",
      .value = "0.0",
      .binding = "EnvironmentLightingUBO",
      .member = "rotation",
      .required = true,
  };
  return feature;
}

FramePass makeSkyboxCompilerPass() {
  FramePass pass;
  pass.name = Pass_SkyboxBackground;
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("render_paths/Skybox/skybox_background");
  pass.reads.push_back(FrameGraphRead::sampled(
      StringID("feature.environmentLighting"), StringID{}));
  return pass;
}

ShaderResourceBinding makeEnvironmentLightingUboBinding() {
  return ShaderResourceBinding{
      "EnvironmentLightingUBO",
      2,
      0,
      ShaderPropertyType::UniformBuffer,
      1,
      32,
      0,
      ShaderStage::Fragment,
      {StructMemberInfo{"color", ShaderPropertyType::Vec3, 0, 12},
       StructMemberInfo{"intensity", ShaderPropertyType::Float, 12, 4},
       StructMemberInfo{"rotation", ShaderPropertyType::Float, 16, 4}}};
}

void testRenderWorkCompilerAcceptsEnvironmentLightingFeatureBindings() {
  Scene scene("EnvironmentCompilerScene");
  const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/environment_lighting"),
          makeCompilerEnvironmentFeature());
  scene.resources().setEnvironmentRuntimeState(SceneEnvironmentRuntimeState{
      .feature = featureHandle, .nodePresent = true, .bakeRequested = true});

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureCubeBinding("SkyboxMap"),
                                         makeEnvironmentLightingUboBinding()},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 41}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 42}},
      });
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = Pass_SkyboxBackground;
  passFacts.shaderProgram.shaderName = "render_paths/Skybox/skybox_background";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;
  passFacts.renderState.depthWriteEnable = false;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  FramePass pass = makeSkyboxCompilerPass();
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "environment feature fullscreen pass should produce one desc");
  if (!descs.empty()) {
    expectAcceptedDescHasBackendPipelineFacts(
        descs.front(),
        "environment feature bindings should satisfy skybox shader");
    EXPECT(hasDescriptorBindingName(descs.front(), StringID("SkyboxMap")),
           "accepted skybox desc should include SkyboxMap");
    EXPECT(hasDescriptorBindingName(descs.front(),
                                    StringID("EnvironmentLightingUBO")),
           "accepted skybox desc should include EnvironmentLightingUBO");
  }
}

void testRenderWorkCompilerRejectsEnvironmentLightingWithoutEnvironmentNode() {
  Scene scene("EnvironmentCompilerScene");
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/environment_lighting"),
          makeCompilerEnvironmentFeature());

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureCubeBinding("SkyboxMap"),
                                         makeEnvironmentLightingUboBinding()},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 45}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 46}},
      });
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = Pass_SkyboxBackground;
  passFacts.shaderProgram.shaderName = "render_paths/Skybox/skybox_background";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  FramePass pass = makeSkyboxCompilerPass();
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1, "missing environment node should produce one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "feature.environmentLighting must reject without environment node");
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MissingResource),
           "missing environment node should report MissingResource");
    EXPECT(hasDiagnosticMessage(
               descs.front(),
               "feature.environmentLighting requires a scene environment node"),
           "missing environment node diagnostic should name required node");
  }
}

void testRenderWorkCompilerRejectsMetadataOnlyEnvironmentFeature() {
  Scene scene("EnvironmentCompilerScene");
  const ResourceIdentityHandle metadataOnlyFeature =
      scene.resources().loadOrGetResource(
          SceneResourceType::RenderFeature,
          ResourceUri("memory://features/node_environment.render-feature"));
  EXPECT(metadataOnlyFeature.isValid(),
         "metadata-only RenderFeature fixture should register");

  [[maybe_unused]] const RenderFeatureHandle unrelatedFeature =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/unrelated_environment_lighting"),
          makeCompilerEnvironmentFeature());
  scene.resources().setEnvironmentRuntimeState(SceneEnvironmentRuntimeState{
      .feature = RenderFeatureHandle{metadataOnlyFeature.index,
                                     metadataOnlyFeature.generation},
      .nodePresent = true,
      .bakeRequested = true});

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureCubeBinding("SkyboxMap"),
                                         makeEnvironmentLightingUboBinding()},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 47}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 48}},
      });
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = Pass_SkyboxBackground;
  passFacts.shaderProgram.shaderName = "render_paths/Skybox/skybox_background";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  FramePass pass = makeSkyboxCompilerPass();
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "metadata-only environment feature should produce one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "metadata-only environment node feature must not be accepted");
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MissingResource),
           "metadata-only environment node feature should report "
           "MissingResource");
    EXPECT(hasDiagnosticMessage(
               descs.front(),
               "feature.environmentLighting RenderFeature payload is "
               "unresolved"),
           "metadata-only environment node feature diagnostic should name "
           "unresolved payload");
  }
}

void testRenderWorkCompilerRejectsWrongLiveEnvironmentFeature() {
  Scene scene("EnvironmentCompilerScene");
  RenderFeature wrongFeature;
  wrongFeature.name = "ToneMapping";
  wrongFeature.feature = "toneMapping";
  const RenderFeatureHandle wrongFeatureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/not_environment_lighting"),
          std::move(wrongFeature));
  EXPECT(wrongFeatureHandle.isValid(),
         "wrong live RenderFeature fixture should register");

  scene.resources().setEnvironmentRuntimeState(
      SceneEnvironmentRuntimeState{.feature = wrongFeatureHandle,
                                   .nodePresent = true,
                                   .bakeRequested = true});

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureCubeBinding("SkyboxMap"),
                                         makeEnvironmentLightingUboBinding()},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 49}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 50}},
      });
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = Pass_SkyboxBackground;
  passFacts.shaderProgram.shaderName = "render_paths/Skybox/skybox_background";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  FramePass pass = makeSkyboxCompilerPass();
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "wrong live environment feature should produce one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "wrong live environment node feature must not be accepted");
    EXPECT(hasDiagnosticMessage(
               descs.front(),
               "scene environment node RenderFeature payload is not "
               "environmentLighting"),
           "wrong live environment node feature diagnostic should name the "
           "feature mismatch");
  }
}

void testRenderWorkCompilerRejectsMissingEnvironmentUboMember() {
  Scene scene("EnvironmentCompilerScene");
  const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/environment_lighting"),
          makeCompilerEnvironmentFeature(/*includeColor=*/false));
  scene.resources().setEnvironmentRuntimeState(SceneEnvironmentRuntimeState{
      .feature = featureHandle, .nodePresent = true, .bakeRequested = false});

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{makeTextureCubeBinding("SkyboxMap"),
                                         makeEnvironmentLightingUboBinding()},
      std::vector<ShaderStageCode>{
          ShaderStageCode{ShaderStage::Vertex,
                          std::vector<u32>{0x07230203, 43}},
          ShaderStageCode{ShaderStage::Fragment,
                          std::vector<u32>{0x07230203, 44}},
      });
  RenderWorkBuildContext::PassPreparationFacts passFacts;
  passFacts.pass = Pass_SkyboxBackground;
  passFacts.shaderProgram.shaderName = "render_paths/Skybox/skybox_background";
  passFacts.shaderProgram.shader = shader;
  passFacts.shaderInfo = shader;

  RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(passFacts);

  RenderWorkCompiler compiler;
  FramePass pass = makeSkyboxCompilerPass();
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtime(scene, std::move(options));
  compiler.buildInputs(pass, context, inputs);
  const auto descs = compiler.prepare(pass, context, inputs);

  EXPECT(descs.size() == 1,
         "missing environment UBO member should produce one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "missing EnvironmentLightingUBO.color should reject desc");
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::MissingBinding),
           "missing environment UBO member should report MissingBinding");
  }
}

void testSceneResourceTableTracksSplitRenderGenerations() {
  SceneResourceTable table;
  const u64 graph0 = table.graphGeneration();
  const u64 resource0 = table.resourceGeneration();
  const u64 feature0 = table.featureGeneration();
  const u64 selection0 = table.descriptorResourceSelectionGeneration();
  const u64 descriptor0 = table.descriptorUploadGeneration();
  const u64 volatile0 = table.volatileUploadGeneration();
  const u64 upload0 = table.uploadGeneration();

  table.markFeatureRuntimeDirty();
  EXPECT(table.graphGeneration() == graph0,
         "feature runtime dirty should not change graph generation");
  EXPECT(table.resourceGeneration() == resource0,
         "feature runtime dirty should not change resource generation");
  EXPECT(table.featureGeneration() == feature0 + 1,
         "feature runtime dirty should advance feature generation");
  EXPECT(table.descriptorResourceSelectionGeneration() == selection0,
         "feature runtime dirty should not advance descriptor resource "
         "selection generation");
  EXPECT(table.descriptorUploadGeneration() == descriptor0 + 1,
         "feature runtime dirty should advance descriptor upload generation");
  EXPECT(table.volatileUploadGeneration() == volatile0,
         "feature runtime dirty should not advance volatile upload generation");
  EXPECT(table.uploadGeneration() == upload0 + 1,
         "feature runtime dirty should keep upload generation semantics");

  const u64 featureDirtySelection =
      table.descriptorResourceSelectionGeneration();
  const u64 featureDirtyDescriptor = table.descriptorUploadGeneration();
  const u64 featureDirtyVolatile = table.volatileUploadGeneration();
  const u64 featureDirtyUpload = table.uploadGeneration();
  table.markBakedResourceDirty();
  EXPECT(table.graphGeneration() == graph0,
         "baked resource dirty should not change graph generation");
  EXPECT(
      table.resourceGeneration() == resource0,
      "baked resource dirty should not change structural resource generation");
  EXPECT(table.featureGeneration() == feature0 + 1,
         "baked resource dirty should not change feature generation");
  EXPECT(table.descriptorResourceSelectionGeneration() ==
             featureDirtySelection + 1,
         "baked resource dirty should advance descriptor resource selection "
         "generation");
  EXPECT(table.descriptorUploadGeneration() == featureDirtyDescriptor + 1,
         "baked resource dirty should advance descriptor upload generation");
  EXPECT(table.volatileUploadGeneration() == featureDirtyVolatile,
         "baked resource dirty should not advance volatile upload generation");
  EXPECT(table.uploadGeneration() == featureDirtyUpload + 1,
         "baked resource dirty should keep upload generation semantics");

  const u64 beforeCameraResource = table.resourceGeneration();
  const u64 beforeCameraSelection =
      table.descriptorResourceSelectionGeneration();
  const u64 beforeCameraDescriptor = table.descriptorUploadGeneration();
  const u64 beforeCameraVolatile = table.volatileUploadGeneration();
  const u64 beforeCameraUpload = table.uploadGeneration();
  const CameraHandle camera = table.registerCamera(CameraResource{});
  EXPECT(camera.isValid(), "registerCamera should produce a live handle");
  EXPECT(table.resourceGeneration() == beforeCameraResource + 1,
         "registerCamera should advance resource generation");
  EXPECT(table.descriptorResourceSelectionGeneration() == beforeCameraSelection,
         "registerCamera should not advance descriptor resource selection "
         "generation");
  EXPECT(table.descriptorUploadGeneration() == beforeCameraDescriptor + 1,
         "registerCamera should advance descriptor upload generation");
  EXPECT(table.volatileUploadGeneration() == beforeCameraVolatile,
         "registerCamera should not advance volatile upload generation");
  EXPECT(table.uploadGeneration() == beforeCameraUpload + 1,
         "registerCamera should advance upload generation");

  const u64 afterCameraRegisterResource = table.resourceGeneration();
  const u64 afterCameraRegisterSelection =
      table.descriptorResourceSelectionGeneration();
  const u64 afterCameraRegisterDescriptor = table.descriptorUploadGeneration();
  const u64 afterCameraRegisterVolatile = table.volatileUploadGeneration();
  const u64 afterCameraRegisterUpload = table.uploadGeneration();
  table.updateCamera(camera, CameraResource{});
  EXPECT(table.resourceGeneration() == afterCameraRegisterResource,
         "updateCamera should leave structural resource generation unchanged");
  EXPECT(table.descriptorResourceSelectionGeneration() ==
             afterCameraRegisterSelection,
         "updateCamera should leave descriptor resource selection generation "
         "unchanged");
  EXPECT(table.descriptorUploadGeneration() == afterCameraRegisterDescriptor,
         "updateCamera should leave descriptor upload generation unchanged");
  EXPECT(table.volatileUploadGeneration() == afterCameraRegisterVolatile + 1,
         "updateCamera should advance volatile upload generation");
  EXPECT(table.uploadGeneration() == afterCameraRegisterUpload + 1,
         "updateCamera should still advance upload generation");

  ObjectResource object;
  const ObjectHandle objectHandle = table.registerObject(object);
  EXPECT(objectHandle.isValid(), "registerObject should produce a live handle");
  const u64 afterObjectRegisterResource = table.resourceGeneration();
  const u64 afterObjectRegisterSelection =
      table.descriptorResourceSelectionGeneration();
  const u64 afterObjectRegisterDescriptor = table.descriptorUploadGeneration();
  const u64 afterObjectRegisterVolatile = table.volatileUploadGeneration();
  const u64 afterObjectRegisterUpload = table.uploadGeneration();
  object.visible = false;
  table.updateObject(objectHandle, object);
  EXPECT(table.resourceGeneration() == afterObjectRegisterResource,
         "value-only updateObject should leave structural resource generation "
         "unchanged");
  EXPECT(
      table.descriptorResourceSelectionGeneration() ==
          afterObjectRegisterSelection + 1,
      "value-only updateObject should refresh prepared descriptor resources");
  EXPECT(table.descriptorUploadGeneration() ==
             afterObjectRegisterDescriptor + 1,
         "value-only updateObject should refresh descriptor upload generation");
  EXPECT(table.volatileUploadGeneration() == afterObjectRegisterVolatile,
         "value-only updateObject should not use the dirty-host-buffer-only "
         "generation");
  EXPECT(table.uploadGeneration() == afterObjectRegisterUpload + 1,
         "value-only updateObject should still advance upload generation");
}

void testSceneRuntimeNodeGenerationTracksIdentityAndHierarchyChanges() {
  auto scene = Scene::create("node_generation");
  auto parent = SceneNode::create("parent");
  auto child = SceneNode::create("child");
  const u64 beforeAdd = scene->runtimeNodeGeneration();
  scene->addRenderable(parent);
  EXPECT(scene->runtimeNodeGeneration() == beforeAdd + 1,
         "scene runtime node generation should advance on add");
  scene->addRenderable(child);

  const u64 initial = scene->runtimeNodeGeneration();
  child->setName("renamed_child");
  EXPECT(scene->runtimeNodeGeneration() == initial + 1,
         "scene runtime node generation should advance on identity changes");

  const u64 afterIdentity = scene->runtimeNodeGeneration();
  child->setParent(parent);
  EXPECT(scene->runtimeNodeGeneration() == afterIdentity + 1,
         "scene runtime node generation should advance on hierarchy changes");

  const u64 afterHierarchy = scene->runtimeNodeGeneration();
  child->setTranslation(Vec3f{1.0f, 2.0f, 3.0f});
  EXPECT(
      scene->runtimeNodeGeneration() == afterHierarchy,
      "scene runtime node generation should not advance on transform changes");

  auto cameraNode = SceneNode::create("camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach to test node");
  scene->addCamera(cameraNode);
  const u64 afterCameraAdd = scene->runtimeNodeGeneration();
  cameraNode->setTranslation(Vec3f{0.0f, 1.0f, 5.0f});
  EXPECT(scene->runtimeNodeGeneration() == afterCameraAdd,
         "scene runtime node generation should not advance on camera transform "
         "changes");
  const u64 beforeCameraPropertyVolatile =
      scene->resources().volatileUploadGeneration();
  const u64 beforeCameraPropertySelection =
      scene->resources().descriptorResourceSelectionGeneration();
  camera->get().setFovY(60.0f);
  EXPECT(scene->runtimeNodeGeneration() == afterCameraAdd,
         "scene runtime node generation should not advance on camera property "
         "changes");
  EXPECT(scene->resources().volatileUploadGeneration() ==
             beforeCameraPropertyVolatile + 1,
         "camera property changes should dirty volatile camera upload data");
  EXPECT(
      scene->resources().descriptorResourceSelectionGeneration() ==
          beforeCameraPropertySelection,
      "camera property changes should not dirty descriptor resource selection");

  const u64 afterValueOnlyChanges = scene->runtimeNodeGeneration();
  scene->removeRenderable(child);
  EXPECT(scene->runtimeNodeGeneration() == afterValueOnlyChanges + 1,
         "scene runtime node generation should advance on removal");
}

void testCameraMembershipChangesDirtyDescriptorSelectionOnly() {
  auto scene = Scene::create("camera_membership_dirty");
  auto cameraNode = SceneNode::create("camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach to test node");
  scene->addCamera(cameraNode);

  const u64 beforeActiveRuntime = scene->runtimeNodeGeneration();
  const u64 beforeActiveSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeActiveDescriptor =
      scene->resources().descriptorUploadGeneration();
  const u64 beforeActiveVolatile =
      scene->resources().volatileUploadGeneration();
  camera->get().setActive(false);
  EXPECT(scene->runtimeNodeGeneration() == beforeActiveRuntime,
         "camera active changes should not advance structural runtime node "
         "generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeActiveSelection + 1,
         "camera active changes should rebuild scene-level camera descriptors");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeActiveDescriptor + 1,
         "camera active changes should rebuild descriptor upload plans");
  EXPECT(scene->resources().volatileUploadGeneration() ==
             beforeActiveVolatile + 1,
         "camera active changes should still refresh the camera UBO payload");

  const u64 beforeTargetRuntime = scene->runtimeNodeGeneration();
  const u64 beforeTargetSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeTargetDescriptor =
      scene->resources().descriptorUploadGeneration();
  camera->get().setTarget(
      RenderTarget{ImageFormat::RGBA8, ImageFormat::D24UnormS8, 1});
  EXPECT(scene->runtimeNodeGeneration() == beforeTargetRuntime,
         "camera target changes should not advance structural runtime node "
         "generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeTargetSelection + 1,
         "camera target changes should rebuild scene-level camera descriptors");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeTargetDescriptor + 1,
         "camera target changes should rebuild descriptor upload plans");

  const u64 beforeMaskRuntime = scene->runtimeNodeGeneration();
  const u64 beforeMaskSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeMaskDescriptor =
      scene->resources().descriptorUploadGeneration();
  camera->get().setCullingMask(Layer_Default);
  EXPECT(scene->runtimeNodeGeneration() == beforeMaskRuntime,
         "camera culling mask changes should not advance structural runtime "
         "node generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeMaskSelection + 1,
         "camera culling mask changes should rebuild render input selection");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeMaskDescriptor + 1,
         "camera culling mask changes should rebuild descriptor upload plans");
}

void testAttachedRenderableComponentLifecycleSyncsResources() {
  auto scene = Scene::create("component_lifecycle");
  auto node = SceneNode::create("attached_mesh_component");
  scene->addRenderable(node);

  const u64 beforeAddRuntime = scene->runtimeNodeGeneration();
  const u64 beforeAddResource = scene->resources().resourceGeneration();
  auto mesh = Mesh::create(
      VertexBuffer<VertexPos>::create(
          std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}}),
      IndexBuffer::create({0, 1, 2}), BoundingBox{{0, 0, 0}, {1, 1, 0}});
  auto meshComponent = node->addComponent<MeshComponent>(mesh);
  EXPECT(meshComponent.has_value(),
         "attached node should accept renderable structure component");
  EXPECT(scene->runtimeNodeGeneration() == beforeAddRuntime + 1,
         "adding attached renderable structure should advance runtime node "
         "generation");
  EXPECT(scene->resources().resourceGeneration() > beforeAddResource,
         "adding attached mesh structure should sync scene mesh resources");
  EXPECT(meshComponent->get().getMeshHandle().isValid(),
         "attached mesh add should register a mesh resource");

  const u64 beforeRemoveRuntime = scene->runtimeNodeGeneration();
  const u64 beforeRemoveResource = scene->resources().resourceGeneration();
  const bool removed = node->removeComponent<MeshComponent>();
  EXPECT(removed, "attached mesh component should be removable");
  EXPECT(scene->runtimeNodeGeneration() == beforeRemoveRuntime + 1,
         "removing attached renderable structure should advance runtime node "
         "generation");
  EXPECT(
      scene->resources().resourceGeneration() > beforeRemoveResource,
      "removing attached mesh structure should release scene mesh resources");
  EXPECT(
      scene->resources().meshCount() == 0,
      "removed attached mesh component should not leave a live mesh resource");
  EXPECT(
      scene->resources().geometryStorageCount() == 0,
      "removed attached mesh component should not leave live geometry storage");
}

void testAttachedMeshReplacementReleasesOldSceneResources() {
  auto scene = Scene::create("mesh_replacement");
  auto node = SceneNode::create("mesh_replace_node");
  auto meshComponent =
      node->addComponent<MeshComponent>(makeSceneTriangleMesh());
  auto materialComponent =
      node->addComponent<MaterialComponent>(makeSceneMaterial("matte"));
  EXPECT(meshComponent.has_value(), "test node should have a mesh component");
  EXPECT(materialComponent.has_value(),
         "test node should have a material component");
  scene->addRenderable(node);

  const MeshHandle oldMesh = meshComponent->get().getMeshHandle();
  const GeometryStorageHandle oldGeometry =
      meshComponent->get().getGeometryStorageHandle();
  const ObjectHandle oldObject = meshComponent->get().getObjectHandle();
  EXPECT(oldMesh.isValid(), "initial mesh should be registered");
  EXPECT(oldGeometry.isValid(), "initial geometry should be registered");
  EXPECT(oldObject.isValid(), "initial object should be registered");
  EXPECT(scene->resources().meshCount() == 1,
         "initial scene should own one mesh");
  EXPECT(scene->resources().geometryStorageCount() == 1,
         "initial scene should own one geometry storage");
  EXPECT(scene->resources().objectCount() == 1,
         "initial scene should own one object");

  meshComponent->get().setMesh(makeSceneTriangleMesh(2.0f));

  EXPECT(!scene->resources().isAlive(oldMesh),
         "mesh replacement should release the old mesh handle");
  EXPECT(!scene->resources().isAlive(oldGeometry),
         "mesh replacement should release the old geometry handle");
  EXPECT(!scene->resources().isAlive(oldObject),
         "mesh replacement should release the old object handle");
  EXPECT(scene->resources().meshCount() == 1,
         "mesh replacement should leave exactly one live mesh");
  EXPECT(scene->resources().geometryStorageCount() == 1,
         "mesh replacement should leave exactly one live geometry storage");
  EXPECT(scene->resources().objectCount() == 1,
         "mesh replacement should leave exactly one live object");
  EXPECT(meshComponent->get().getMeshHandle().isValid(),
         "mesh replacement should register a new mesh handle");
  EXPECT(meshComponent->get().getObjectHandle().isValid(),
         "mesh replacement should register a replacement object handle");
}

void testAttachedMaterialReplacementReleasesOldMaterialTextures() {
  auto scene = Scene::create("material_replacement");
  const usize baselineTextureCount = scene->resources().textureCount();
  auto node = SceneNode::create("material_replace_node");
  auto meshComponent =
      node->addComponent<MeshComponent>(makeSceneTriangleMesh());
  auto materialComponent = node->addComponent<MaterialComponent>(
      makeTexturedSceneMaterial("first_textured"));
  EXPECT(meshComponent.has_value(), "test node should have a mesh component");
  EXPECT(materialComponent.has_value(),
         "test node should have a material component");
  scene->addRenderable(node);

  const MaterialHandle oldMaterial =
      materialComponent->get().getMaterialHandle();
  const auto oldMaterialRef = scene->resources().resolve(oldMaterial);
  EXPECT(oldMaterial.isValid(), "initial material should be registered");
  EXPECT(oldMaterialRef.has_value(), "initial material should resolve");
  const TextureHandle oldTexture =
      oldMaterialRef.has_value()
          ? oldMaterialRef->get().getTextureHandle(StringID("BaseColorMap"))
          : TextureHandle{};
  EXPECT(oldTexture.isValid(), "initial material texture should be registered");
  EXPECT(scene->resources().materialCount() == 1,
         "initial scene should own one material");
  EXPECT(scene->resources().textureCount() == baselineTextureCount + 1,
         "initial scene should own one material texture");
  EXPECT(scene->resources().objectCount() == 1,
         "initial scene should own one material-bound object");

  materialComponent->get().setMaterialInstance(
      makeTexturedSceneMaterial("second_textured"));

  EXPECT(!scene->resources().isAlive(oldMaterial),
         "material replacement should release the old material handle");
  EXPECT(
      !scene->resources().isAlive(oldTexture),
      "material replacement should release old table-owned material texture");
  EXPECT(scene->resources().materialCount() == 1,
         "material replacement should leave exactly one live material");
  EXPECT(scene->resources().textureCount() == baselineTextureCount + 1,
         "material replacement should leave exactly one live material texture");
  EXPECT(scene->resources().objectCount() == 1,
         "material replacement should update the existing object instead of "
         "leaking another object");
}

void testLightPropertyChangesDirtyDescriptorSelectionOnly() {
  auto scene = Scene::create("light_dirty");
  auto lightNode = SceneNode::create("point_light");
  scene->addRenderable(lightNode);
  scene->attachLight(lightNode, std::make_shared<PointLight>());
  auto light = scene->getPointLight(*lightNode);
  EXPECT(light.has_value(), "attached point light should be resolvable");
  if (!light.has_value()) {
    return;
  }

  const u64 beforeRuntime = scene->runtimeNodeGeneration();
  const u64 beforeSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeDescriptor = scene->resources().descriptorUploadGeneration();
  const u64 beforeVolatile = scene->resources().volatileUploadGeneration();
  light->get().setIntensity(2.0f);

  EXPECT(scene->runtimeNodeGeneration() == beforeRuntime,
         "light property changes should not advance runtime node generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeSelection + 1,
         "light property changes should refresh prepared descriptor resource "
         "selection");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeDescriptor + 1,
         "light property changes should refresh descriptor upload plans");
  EXPECT(scene->resources().volatileUploadGeneration() == beforeVolatile,
         "point light aggregate updates should not use dirty-host-buffer-only "
         "generation");
}

void testLightPassMembershipDirtiesDescriptorSelectionOnly() {
  auto scene = Scene::create("light_pass_dirty");
  auto lightNode = SceneNode::create("point_light_passes");
  scene->addRenderable(lightNode);
  scene->attachLight(lightNode, std::make_shared<PointLight>());
  auto light = scene->getPointLight(*lightNode);
  EXPECT(light.has_value(), "attached point light should be resolvable");
  if (!light.has_value()) {
    return;
  }

  const u64 beforeRuntime = scene->runtimeNodeGeneration();
  const u64 beforeSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeDescriptor = scene->resources().descriptorUploadGeneration();
  const u64 beforeVolatile = scene->resources().volatileUploadGeneration();
  light->get().setSupportedPasses({Pass_Forward});

  EXPECT(scene->runtimeNodeGeneration() == beforeRuntime,
         "light pass membership should not advance runtime node generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeSelection + 1,
         "light pass membership should refresh prepared descriptor resource "
         "selection");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeDescriptor + 1,
         "light pass membership should refresh descriptor upload plans");
  EXPECT(
      scene->resources().volatileUploadGeneration() == beforeVolatile,
      "light pass membership should not use dirty-host-buffer-only generation");
}

void testDirectionalCascadeRefreshDoesNotDirtyDescriptorSelection() {
  auto scene = Scene::create("directional_cascade_dirty");
  auto cameraNode = SceneNode::create("camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach to test node");
  scene->addCamera(cameraNode);

  auto lightNode = SceneNode::create("directional_light");
  scene->addRenderable(lightNode);
  scene->attachLight(lightNode, std::make_shared<DirectionalLight>());
  auto light = scene->getDirectionalLight(*lightNode);
  EXPECT(light.has_value(), "attached directional light should resolve");
  if (!light.has_value() || !camera.has_value()) {
    return;
  }

  light->get().updateShadowCascadesForCamera(camera->get());
  const u64 beforeRepeatSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeRepeatDescriptor =
      scene->resources().descriptorUploadGeneration();
  light->get().updateShadowCascadesForCamera(camera->get());

  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeRepeatSelection,
         "steady directional cascade refresh should not rebuild render inputs");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeRepeatDescriptor,
         "steady directional cascade refresh should not rebuild descriptor "
         "upload plans");
}

void testLightNodeTransformDirtiesDescriptorSelectionOnly() {
  auto scene = Scene::create("light_transform_dirty");
  auto lightNode = SceneNode::create("point_light_transform");
  scene->addRenderable(lightNode);
  scene->attachLight(lightNode, std::make_shared<PointLight>());

  const u64 beforeRuntime = scene->runtimeNodeGeneration();
  const u64 beforeSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeDescriptor = scene->resources().descriptorUploadGeneration();
  lightNode->setTranslation(Vec3f{2.0f, 3.0f, 4.0f});

  EXPECT(
      scene->runtimeNodeGeneration() == beforeRuntime,
      "light transform should not advance structural runtime node generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() ==
             beforeSelection + 1,
         "light transform should refresh aggregate light descriptor selection");
  EXPECT(scene->resources().descriptorUploadGeneration() ==
             beforeDescriptor + 1,
         "light transform should refresh descriptor upload plans");
}

void testDirectionalLightNodeTransformRefreshesDirectionalUbo() {
  auto scene = Scene::create("directional_light_transform_ubo");
  auto lightNode = SceneNode::create("directional_light_transform");
  scene->addRenderable(lightNode);
  scene->attachLight(lightNode, std::make_shared<DirectionalLight>());
  auto light = scene->getDirectionalLight(*lightNode);
  EXPECT(light.has_value(), "attached directional light should be resolvable");
  if (!light.has_value()) {
    return;
  }

  light->get().setSupportedPasses({Pass_Forward});
  light->get().getDirectionalUBO().clearDirty();
  lightNode->setRotation(
      Quatf::fromAxisAngle(Vec3f{0.0f, 1.0f, 0.0f}, 1.57079632679f));

  const Vec3f expectedDirection = light->get().getDirection();
  const Vec4f uboDirection = light->get().getDirectionalUBO().param.dir;
  EXPECT(
      approxEqual(uboDirection.x, expectedDirection.x) &&
          approxEqual(uboDirection.y, expectedDirection.y) &&
          approxEqual(uboDirection.z, expectedDirection.z),
      "directional light transform should refresh the per-light UBO direction");
  EXPECT(light->get().getDirectionalUBO().isDirty(),
         "directional light transform should mark the per-light UBO dirty");
}

void testParentTransformSyncsDescendantRuntimeResources() {
  auto scene = Scene::create("parent_transform_dirty");
  auto parent = SceneNode::create("parent");
  scene->addRenderable(parent);

  auto meshChild = SceneNode::create("mesh_child");
  auto meshComponent =
      meshChild->addComponent<MeshComponent>(makeSceneTriangleMesh());
  auto materialComponent =
      meshChild->addComponent<MaterialComponent>(makeSceneMaterial("matte"));
  EXPECT(meshComponent.has_value(), "mesh child should have mesh component");
  EXPECT(materialComponent.has_value(),
         "mesh child should have material component");
  meshChild->setParent(parent);
  scene->addRenderable(meshChild);

  auto cameraChild = SceneNode::create("camera_child");
  auto camera = cameraChild->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera child should have camera component");
  cameraChild->setParent(parent);
  scene->addCamera(cameraChild);

  auto lightChild = SceneNode::create("light_child");
  lightChild->setParent(parent);
  scene->addRenderable(lightChild);
  scene->attachLight(lightChild, std::make_shared<PointLight>());

  const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
  EXPECT(objectHandle.isValid(), "mesh child should have an object handle");

  const u64 beforeRuntime = scene->runtimeNodeGeneration();
  const u64 beforeSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeDescriptor = scene->resources().descriptorUploadGeneration();
  const u64 beforeVolatile = scene->resources().volatileUploadGeneration();
  parent->setTranslation(Vec3f{4.0f, 5.0f, 6.0f});

  const auto object = scene->resources().resolve(objectHandle);
  EXPECT(object.has_value(), "mesh child object should still resolve");
  const Vec3f objectTranslation =
      object.has_value()
          ? Transform::fromMat4(object->get().objectToWorld).translation
          : Vec3f{};

  EXPECT(
      scene->runtimeNodeGeneration() == beforeRuntime,
      "parent transform should not advance structural runtime node generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() >
             beforeSelection,
         "parent transform should refresh descendant object/light descriptor "
         "selection");
  EXPECT(scene->resources().descriptorUploadGeneration() > beforeDescriptor,
         "parent transform should refresh descendant descriptor upload plans");
  EXPECT(scene->resources().volatileUploadGeneration() > beforeVolatile,
         "parent transform should refresh descendant camera UBO data");
  EXPECT(approxEqual(objectTranslation.x, 4.0f) &&
             approxEqual(objectTranslation.y, 5.0f) &&
             approxEqual(objectTranslation.z, 6.0f),
         "parent transform should update descendant object world transform");
}

void testHierarchyChangeSyncsRuntimeResources() {
  auto scene = Scene::create("hierarchy_dirty");
  auto firstParent = SceneNode::create("first_parent");
  firstParent->setTranslation(Vec3f{1.0f, 0.0f, 0.0f});
  auto secondParent = SceneNode::create("second_parent");
  secondParent->setTranslation(Vec3f{8.0f, 0.0f, 0.0f});
  scene->addRenderable(firstParent);
  scene->addRenderable(secondParent);

  auto child = SceneNode::create("hierarchy_child");
  auto meshComponent =
      child->addComponent<MeshComponent>(makeSceneTriangleMesh());
  auto materialComponent =
      child->addComponent<MaterialComponent>(makeSceneMaterial("matte"));
  EXPECT(meshComponent.has_value(),
         "hierarchy child should have mesh component");
  EXPECT(materialComponent.has_value(),
         "hierarchy child should have material component");
  child->setParent(firstParent);
  scene->addRenderable(child);

  const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
  EXPECT(objectHandle.isValid(), "hierarchy child should have object handle");

  const u64 beforeRuntime = scene->runtimeNodeGeneration();
  const u64 beforeSelection =
      scene->resources().descriptorResourceSelectionGeneration();
  const u64 beforeDescriptor = scene->resources().descriptorUploadGeneration();
  child->setParent(secondParent);

  const auto object = scene->resources().resolve(objectHandle);
  EXPECT(object.has_value(), "hierarchy child object should still resolve");
  const Vec3f objectTranslation =
      object.has_value()
          ? Transform::fromMat4(object->get().objectToWorld).translation
          : Vec3f{};

  EXPECT(scene->runtimeNodeGeneration() == beforeRuntime + 1,
         "hierarchy changes should advance structural runtime node generation");
  EXPECT(scene->resources().descriptorResourceSelectionGeneration() >
             beforeSelection,
         "hierarchy changes should refresh descendant object descriptor "
         "selection");
  EXPECT(scene->resources().descriptorUploadGeneration() > beforeDescriptor,
         "hierarchy changes should refresh descriptor upload plans");
  EXPECT(approxEqual(objectTranslation.x, 8.0f) &&
             approxEqual(objectTranslation.y, 0.0f) &&
             approxEqual(objectTranslation.z, 0.0f),
         "hierarchy changes should update object world transform");
}

void testParentTransformDoesNotRegisterUnattachedDescendants() {
  auto scene = Scene::create("unattached_descendant");
  auto parent = SceneNode::create("parent");
  scene->addRenderable(parent);

  auto unattachedChild = SceneNode::create("unattached_child");
  auto meshComponent =
      unattachedChild->addComponent<MeshComponent>(makeSceneTriangleMesh());
  auto materialComponent = unattachedChild->addComponent<MaterialComponent>(
      makeSceneMaterial("unattached"));
  EXPECT(meshComponent.has_value(),
         "unattached child should have mesh component");
  EXPECT(materialComponent.has_value(),
         "unattached child should have material component");
  unattachedChild->setParent(parent);

  const usize beforeMeshCount = scene->resources().meshCount();
  const usize beforeMaterialCount = scene->resources().materialCount();
  const usize beforeObjectCount = scene->resources().objectCount();
  parent->setTranslation(Vec3f{1.0f, 2.0f, 3.0f});

  EXPECT(
      scene->resources().meshCount() == beforeMeshCount,
      "parent transform should not register unattached child mesh resources");
  EXPECT(scene->resources().materialCount() == beforeMaterialCount,
         "parent transform should not register unattached child materials");
  EXPECT(scene->resources().objectCount() == beforeObjectCount,
         "parent transform should not register unattached child objects");
  EXPECT(!meshComponent->get().getMeshHandle().isValid(),
         "unattached child should not receive a scene mesh handle");
}

} // namespace

int main() {
  testRenderInputPayloadDoesNotExposePipelineFacts();
  testDescReferencesInputWithoutOwningPayload();
  testDescStatusDefaultsAndStats();
  testComputePayloadRemainsOnInput();
  testDescCarriesPipelineFacts();
  testFullscreenTriangleBuildsOneInputAndDesc();
  testPrepareReferencesInputWithoutCopyingDrawCommands();
  testFullscreenDescStatsAndSkeletonPipelineFactsAreRejected();
  testComputeInputWithoutShaderFactsIsRejected();
  testFullscreenDescUsesPreparedPassFactsAndGraphReads();
  testComputeDescUsesPreparedPassFacts();
  testRenderWorkCompilerRejectsMetadataOnlyBakeSourcePayload();
  testRenderWorkCompilerRejectsBakeComputePayloadWithoutReadback();
  testSceneRenderableValidatedShaderFactsPreparePipelineDesc();
  testSceneRenderableIncludesCameraSceneResourceBinding();
  testSceneRenderableRejectsUnresolvedRequiredBinding();
  testSceneRenderablePipelineKeyUsesMaterialVariantNotTypeSignature();
  testPipelineKeyIncludesGenericPassSpecializationValue();
  testRenderWorkCompilerResolvesPassFeatureSpecializationFromReflection();
  testRenderWorkCompilerExcludesVolatilePassFieldsFromSpecialization();
  testRenderWorkCompilerSharesSurfaceLightingPayloadAcrossForwardAndDeferred();
  testRenderWorkCompilerRejectsSurfaceLightingUboWithoutFeatureRead();
  testRenderWorkCompilerRejectsIblSurfaceLightingWithoutEnvironmentBake();
  testRenderWorkCompilerRejectsIblSurfaceLightingWithoutMaterialIblBake();
  testSceneRenderableMissingRequiredMaterialProducesRejectedDesc();
  testSceneRenderableMissingMaterialDoesNotUseSupportsPassAsSelection();
  testNoMaterialDebugRenderableAcceptedWithDrawPayload();
  testDebugObjectClassDoesNotRequireDebugOverlayPassName();
  testDebugMeshObjectClassAcceptsNoMaterialDebugRenderable();
  testShadowPassNameDoesNotOverrideZeroVisibleMask();
  testPassNameDoesNotCreateDefaultVisibilityWithoutCamera();
  testUnsupportedObjectClassProducesRejectedDesc();
  testMaterialTypeFilterRejectsNoMaterialRenderable();
  testRenderWorkCompilerRejectsEnvironmentLightingWithoutEnvironmentNode();
  testRenderWorkCompilerRejectsMetadataOnlyEnvironmentFeature();
  testRenderWorkCompilerRejectsWrongLiveEnvironmentFeature();
  testRenderWorkCompilerAcceptsEnvironmentLightingFeatureBindings();
  testRenderWorkCompilerRejectsMissingEnvironmentUboMember();
  testSceneResourceTableTracksSplitRenderGenerations();
  testSceneRuntimeNodeGenerationTracksIdentityAndHierarchyChanges();
  testCameraMembershipChangesDirtyDescriptorSelectionOnly();
  testAttachedRenderableComponentLifecycleSyncsResources();
  testAttachedMeshReplacementReleasesOldSceneResources();
  testAttachedMaterialReplacementReleasesOldMaterialTextures();
  testLightPropertyChangesDirtyDescriptorSelectionOnly();
  testLightPassMembershipDirtiesDescriptorSelectionOnly();
  testDirectionalCascadeRefreshDoesNotDirtyDescriptorSelection();
  testLightNodeTransformDirtiesDescriptorSelectionOnly();
  testDirectionalLightNodeTransformRefreshesDirectionalUbo();
  testParentTransformSyncsDescendantRuntimeResources();
  testHierarchyChangeSyncsRuntimeResources();
  testParentTransformDoesNotRegisterUnattachedDescendants();
  return g_failures == 0 ? 0 : 1;
}
