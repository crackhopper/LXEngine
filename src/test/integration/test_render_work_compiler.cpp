#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/scene.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <utility>

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
  desc.stats.inputCount = 2;
  desc.stats.acceptedInputCount = 1;
  desc.stats.rejectedInputCount = 1;
  desc.stats.submittedDrawCount = 3;
  desc.stats.submittedDispatchCount = 0;
  desc.stats.fallbackObservedCount = 1;

  EXPECT(desc.diagnostics.size() == 1, "desc should carry input diagnostics");
  EXPECT(desc.diagnostics.front().code ==
             RenderInputDiagnosticCode::MissingPipelineFacts,
         "desc diagnostic code should round-trip");
  EXPECT(desc.stats.inputCount == 2, "desc stats should carry input count");
  EXPECT(desc.stats.acceptedInputCount == 1,
         "desc stats should carry accepted input count");
  EXPECT(desc.stats.rejectedInputCount == 1,
         "desc stats should carry rejected input count");
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
  pass.shaderUri = ResourceUri("post_process");

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
  EXPECT(descs.size() == 1 && descs.front().accepted(),
         "fullscreen input should prepare one accepted desc");
}

void testPrepareReferencesInputWithoutCopyingDrawCommands() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("post_process");

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

void testFullscreenDescStatsPipelineAndShaderFactsArePopulated() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("post_process");

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
  EXPECT(desc.shaderUri == StringID("post_process"),
         "fullscreen desc should carry shader uri");
  EXPECT(desc.stats.inputCount == 1, "stats should count one input");
  EXPECT(desc.stats.acceptedInputCount == 1,
         "stats should count one accepted input");
  EXPECT(desc.stats.rejectedInputCount == 0,
         "stats should count no rejected inputs");
  EXPECT(desc.stats.submittedDrawCount == 1,
         "stats should count submitted draw commands");
  EXPECT(desc.stats.submittedDispatchCount == 0,
         "stats should count no compute dispatches");
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
    EXPECT(descs.front().diagnostics.front().code ==
               RenderInputDiagnosticCode::MaterialRequired,
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
    EXPECT(descs.front().diagnostics.front().code ==
               RenderInputDiagnosticCode::MaterialRequired,
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
  pass.shaderUri = ResourceUri("debug_overlay");

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
  EXPECT(descs.size() == 1 && descs.front().accepted(),
         "debug no-material renderable should prepare an accepted desc");
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
  EXPECT(descs.size() == 1 && descs.front().accepted(),
         "debug object class should accept a custom pass name");
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
  EXPECT(descs.size() == 1 && descs.front().accepted(),
         "debug.mesh no-material renderable should prepare an accepted desc");
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
    EXPECT(descs.front().diagnostics.front().code ==
               RenderInputDiagnosticCode::ObjectClassRejected,
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
  pass.shaderUri = ResourceUri("debug_overlay");

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
    EXPECT(descs.front().diagnostics.front().code ==
               RenderInputDiagnosticCode::MaterialTypeRejected,
           "diagnostic should be MaterialTypeRejected");
  }
}

} // namespace

int main() {
  testDescReferencesInputWithoutOwningPayload();
  testDescStatusDefaultsAndStats();
  testComputePayloadRemainsOnInput();
  testDescCarriesPipelineFacts();
  testFullscreenTriangleBuildsOneInputAndDesc();
  testPrepareReferencesInputWithoutCopyingDrawCommands();
  testFullscreenDescStatsPipelineAndShaderFactsArePopulated();
  testSceneRenderableMissingRequiredMaterialProducesRejectedDesc();
  testSceneRenderableMissingMaterialDoesNotUseSupportsPassAsSelection();
  testNoMaterialDebugRenderableAcceptedWithDrawPayload();
  testDebugObjectClassDoesNotRequireDebugOverlayPassName();
  testDebugMeshObjectClassAcceptsNoMaterialDebugRenderable();
  testShadowPassNameDoesNotOverrideZeroVisibleMask();
  testPassNameDoesNotCreateDefaultVisibilityWithoutCamera();
  testUnsupportedObjectClassProducesRejectedDesc();
  testMaterialTypeFilterRejectsNoMaterialRenderable();
  return g_failures == 0 ? 0 : 1;
}
