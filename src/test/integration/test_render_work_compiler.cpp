#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
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
concept HasDescriptorResources = requires(T value) { value.descriptorResources; };

ShaderResourceBinding makeUniformBinding(std::string name,
                                         u32 binding = 0) {
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
  return ShaderResourceBinding{std::move(name),
                               0,
                               binding,
                               ShaderPropertyType::Texture2D,
                               1,
                               0,
                               0,
                               ShaderStage::Fragment,
                               {}};
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
  bool supportsPass
  (StringID) const override {
    return m_passSupported;
  }
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
             std::vector<VertexInputAttribute> vertexInputs = {})
      : m_bindings(std::move(bindings)), m_stages(std::move(stages)),
        m_vertexInputs(std::move(vertexInputs)) {}

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
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32, u32) const override {
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &) const override {
    return std::nullopt;
  }
  usize getProgramHash() const override { return 0x1234u; }
  std::string getShaderName() const override { return "validated_fake_shader"; }

private:
  std::vector<ShaderResourceBinding> m_bindings;
  std::vector<ShaderStageCode> m_stages;
  std::vector<VertexInputAttribute> m_vertexInputs;
};

class ValidatedRenderable final : public IRenderable {
public:
  explicit ValidatedRenderable(
      StringID pass, std::string variantValue = "1",
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
        ShaderStageCode{ShaderStage::Vertex,
                        std::vector<u32>{0x07230203, 11}},
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
    m_data.materialTypeSignature = StringID(std::move(materialTypeSignatureName));
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

bool hasFatalPipelineDiagnostic(const RenderInputDesc &desc) {
  return hasDiagnosticCode(desc,
                           RenderInputDiagnosticCode::MissingShaderReflection) ||
         hasDiagnosticCode(desc, RenderInputDiagnosticCode::MissingPipelineFacts);
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
  return std::any_of(desc.resourceDependencies.begin(),
                     desc.resourceDependencies.end(),
                     [bindingName](const GpuResourceRef &resource) {
                       return resource.isValid() &&
                              resource.getBindingName() == bindingName;
                     });
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
  EXPECT(desc.stats.compilerInputCount == 2, "desc stats should carry compiler input count");
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
    EXPECT(hasDiagnosticCode(descs.front(),
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
  EXPECT(hasDiagnosticCode(desc, RenderInputDiagnosticCode::MissingPipelineFacts),
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

  const auto *draw = dynamic_cast<const RenderDrawInput *>(inputs.front().get());
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
  EXPECT(hasDiagnosticCode(desc, RenderInputDiagnosticCode::MissingBinding) ||
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
    EXPECT(hasFatalPipelineDiagnostic(descs.front()),
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
  EXPECT(descs.size() == 1,
         "debug object class should still produce one desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
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

RenderFeature makeCompilerEnvironmentFeature(
    bool includeColor = true, std::string backgroundModeValue = "infinite") {
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
  feature.parameters["backgroundMode"] = RenderFeatureParameter{
      .kind = "enum",
      .value = std::move(backgroundModeValue),
      .binding = "EnvironmentLightingUBO",
      .member = "backgroundMode",
      .required = true,
      .allowedValues = {"none", "infinite", "finiteBox"},
  };
  feature.parameters["finiteBoxBounds"] = RenderFeatureParameter{
      .kind = "vec6",
      .value = "[-5.0, 5.0, -2.0, 3.0, -5.0, 5.0]",
      .requiredWhenParameter = "backgroundMode",
      .requiredWhenEquals = "finiteBox",
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
       StructMemberInfo{"rotation", ShaderPropertyType::Float, 16, 4},
       StructMemberInfo{"backgroundMode", ShaderPropertyType::Float, 20, 4}}};
}

void testRenderWorkCompilerAcceptsEnvironmentLightingFeatureBindings() {
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

void testRenderWorkCompilerRejectsFiniteBoxOnFullscreenSkyboxPass() {
  Scene scene("EnvironmentCompilerScene");
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
          ResourceUri("memory://features/environment_lighting"),
          makeCompilerEnvironmentFeature(/*includeColor=*/true,
                                         "finiteBox"));

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
         "finiteBox fullscreen pass should still produce one rejected desc");
  if (!descs.empty()) {
    EXPECT(!descs.front().accepted(),
           "finiteBox must not render through fullscreen SkyboxBackground");
    EXPECT(hasDiagnosticCode(descs.front(),
                             RenderInputDiagnosticCode::UnsupportedInputContract),
           "finiteBox fullscreen rejection should report unsupported input");
  }
}

void testRenderWorkCompilerRejectsMissingEnvironmentUboMember() {
  Scene scene("EnvironmentCompilerScene");
  [[maybe_unused]] const RenderFeatureHandle featureHandle =
      scene.resources().registerRenderFeature(
      ResourceUri("memory://features/environment_lighting"),
      makeCompilerEnvironmentFeature(/*includeColor=*/false));

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
    EXPECT(hasDiagnosticCode(descs.front(), RenderInputDiagnosticCode::MissingBinding),
           "missing environment UBO member should report MissingBinding");
  }
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
  testSceneRenderableValidatedShaderFactsPreparePipelineDesc();
  testSceneRenderableIncludesCameraSceneResourceBinding();
  testSceneRenderableRejectsUnresolvedRequiredBinding();
  testSceneRenderablePipelineKeyUsesMaterialVariantNotTypeSignature();
  testSceneRenderableMissingRequiredMaterialProducesRejectedDesc();
  testSceneRenderableMissingMaterialDoesNotUseSupportsPassAsSelection();
  testNoMaterialDebugRenderableAcceptedWithDrawPayload();
  testDebugObjectClassDoesNotRequireDebugOverlayPassName();
  testDebugMeshObjectClassAcceptsNoMaterialDebugRenderable();
  testShadowPassNameDoesNotOverrideZeroVisibleMask();
  testPassNameDoesNotCreateDefaultVisibilityWithoutCamera();
  testUnsupportedObjectClassProducesRejectedDesc();
  testMaterialTypeFilterRejectsNoMaterialRenderable();
  testRenderWorkCompilerAcceptsEnvironmentLightingFeatureBindings();
  testRenderWorkCompilerRejectsFiniteBoxOnFullscreenSkyboxPass();
  testRenderWorkCompilerRejectsMissingEnvironmentUboMember();
  return g_failures == 0 ? 0 : 1;
}
