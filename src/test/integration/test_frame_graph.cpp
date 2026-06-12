#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/parameter_buffer.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "scene_test_helpers.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

bool approx(float a, float b, float epsilon = 0.001f) {
  return std::abs(a - b) <= epsilon;
}

bool approxMatrix(const Mat4f &a, const Mat4f &b, float epsilon = 0.001f) {
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!approx(a(row, col), b(row, col), epsilon)) {
        return false;
      }
    }
  }
  return true;
}

usize countResourcesNamed(const DescriptorResourceList &resources,
                          StringID bindingName) {
  return static_cast<usize>(
      std::count_if(resources.begin(), resources.end(),
                    [bindingName](const DescriptorResourceRef &resource) {
                      return resource.getBindingName() == bindingName;
                    }));
}

std::optional<std::reference_wrapper<const DescriptorResourceRef>>
findResourceNamed(const DescriptorResourceList &resources,
                  StringID bindingName) {
  const auto it =
      std::find_if(resources.begin(), resources.end(),
                   [bindingName](const DescriptorResourceRef &resource) {
                     return resource.getBindingName() == bindingName;
                   });
  if (it == resources.end()) {
    return std::nullopt;
  }
  return std::cref(*it);
}

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

class FakeShader : public IShader {
public:
  void setBindings(std::vector<ShaderResourceBinding> bindings) {
    m_bindings = std::move(bindings);
  }

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }
  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32, u32) const override {
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &) const override {
    return std::nullopt;
  }
  usize getProgramHash() const override { return 0; }
  std::string getShaderName() const override { return "fake_fg"; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

std::shared_ptr<SceneNode>
makeRenderable(const std::string &shaderName = "fake_fg",
               const std::vector<ShaderVariant> &variants = {},
               bool enableShadow = false,
               const RenderState &renderState = RenderState{}) {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  auto mesh = Mesh::create(vb, ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});

  auto shader = std::make_shared<FakeShader>();
  auto tmpl = MaterialTemplate::create(shaderName);

  ShaderProgramSet ps;
  ps.shaderName = shaderName;
  ps.variants = variants;
  ps.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = ps;
  entry.renderState = renderState;
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  MaterialPassDefinition shadowEntry;
  shadowEntry.shaderProgram = ps;
  shadowEntry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Shadow, std::move(shadowEntry));

  auto material = MaterialInstance::create(tmpl);
  if (!enableShadow) {
    material->setPassEnabled(Pass_Shadow, false);
  }
  static int nodeCounter = 0;
  auto node = SceneNode::create("fg_node_" + std::to_string(++nodeCounter));
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);
  return node;
}

std::shared_ptr<SceneNode> makeTransparentRenderable(const std::string &name,
                                                     const Vec3f &translation) {
  RenderState transparentState;
  transparentState.depthWriteEnable = false;
  transparentState.blendEnable = true;
  transparentState.srcBlend = BlendFactor::SrcAlpha;
  transparentState.dstBlend = BlendFactor::OneMinusSrcAlpha;
  auto node = makeRenderable(name, {}, false, transparentState);
  node->setTranslation(translation);
  return node;
}

std::shared_ptr<SceneNode>
makeRenderableWithMask(VisibilityLayerMask mask,
                       const std::string &shaderName = "fake_fg") {
  auto node = makeRenderable(shaderName);
  node->setVisibilityLayerMask(mask);
  return node;
}

std::shared_ptr<SceneNode> makeIblRenderable() {
  auto node = makeRenderable("fake_fg_ibl");
  auto materialComponent = node->getComponent<MaterialComponent>();
  if (!materialComponent) {
    return node;
  }
  auto material = materialComponent->get().getPendingMaterialInstance();
  auto shader = std::dynamic_pointer_cast<FakeShader>(
      material->getPassShader(Pass_Forward));
  if (shader) {
    shader->setBindings({
        ShaderResourceBinding{"IrradianceMap", 3, 0,
                              ShaderPropertyType::TextureCube},
        ShaderResourceBinding{"PrefilteredEnvMap", 3, 1,
                              ShaderPropertyType::TextureCube},
        ShaderResourceBinding{"BrdfLut", 3, 2, ShaderPropertyType::Texture2D},
        ShaderResourceBinding{"EnvironmentUBO", 3, 3,
                              ShaderPropertyType::UniformBuffer, 1, 16},
    });
  }
  return node;
}

std::shared_ptr<SceneNode> makeMeshOverlayRenderable() {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  auto mesh = Mesh::create(vb, ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});

  auto shader = std::make_shared<FakeShader>();
  auto tmpl = MaterialTemplate::create("mesh_overlay_test");

  ShaderProgramSet ps;
  ps.shaderName = "mesh_overlay_test";
  ps.shader = shader;

  MaterialPassDefinition entry;
  entry.shaderProgram = ps;
  entry.renderState = RenderState{};
  entry.meshOverlay.enabled = true;
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));

  auto node = SceneNode::create("fg_mesh_overlay_node");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(MaterialInstance::create(tmpl));
  return node;
}

// Helpers for REQ-009 scenarios.
SceneNodeSharedPtr makeCameraWithTarget(const RenderTarget &target) {
  static int cameraCounter = 0;
  auto node =
      SceneNode::create("fg_camera_target_" + std::to_string(++cameraCounter));
  auto camera = node->addComponent<CameraComponent>();
  camera->get().setTarget(target);
  camera->get().updateMatrices();
  return node;
}

SceneNodeSharedPtr makeCameraWithTargetAndMask(const RenderTarget &target,
                                               VisibilityLayerMask mask) {
  auto node = makeCameraWithTarget(target);
  node->getComponent<CameraComponent>()->get().setCullingMask(mask);
  return node;
}

SceneNodeSharedPtr makeCameraNoTarget() {
  static int cameraCounter = 0;
  auto node = SceneNode::create("fg_camera_no_target_" +
                                std::to_string(++cameraCounter));
  node->addComponent<CameraComponent>();
  return node;
}

std::shared_ptr<DirectionalLight>
makeLightWithPasses(std::initializer_list<StringID> passes) {
  auto light = std::make_shared<DirectionalLight>();
  light->setSupportedPasses(passes);
  return light;
}

SceneNodeSharedPtr
attachLightNode(const SceneSharedPtr &scene,
                const std::shared_ptr<DirectionalLight> &light,
                const std::string &name) {
  auto node = SceneNode::create(name + "_node");
  node->setName(name);
  scene->addRenderable(node);
  scene->attachLight(node, light);
  return node;
}

SceneSharedPtr
makeSceneWithDefaultCamera(const SceneNodeSharedPtr &root = nullptr) {
  auto scene = Scene::create(root);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  return scene;
}

RenderPassNode makeRenderPassNode(std::string id,
                                  std::vector<std::string> sources,
                                  std::vector<std::string> targets) {
  RenderPassNode pass;
  pass.id = std::move(id);
  pass.shaderUri = "techniques/Forward/frame_graph_build_plan_test";
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.sources = std::move(sources);
  pass.targets = std::move(targets);
  return pass;
}

bool throwsInvalidArgument(const std::function<void()> &fn,
                           const std::string &needle) {
  try {
    fn();
  } catch (const std::invalid_argument &e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testSingleRenderableSinglePass() {
  auto r = makeRenderable();
  auto scene = makeSceneWithDefaultCamera(r);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 1, "single renderable → 1 build info");
  EXPECT(fg.getPasses()[0].queue.getItems().size() == 1,
         "queue has exactly one item");
}

void testDuplicateRenderablesDedupe() {
  auto r1 = makeRenderable();
  auto r2 = makeRenderable(); // same config
  auto scene = makeSceneWithDefaultCamera(r1);
  scene->addRenderable(r2);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  EXPECT(fg.getPasses()[0].queue.getItems().size() == 2, "two items in queue");
  auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 1,
         "duplicate configs collapse to one PipelineBuildDesc");
}

void testDifferentVariantKeepsTwo() {
  auto r1 = makeRenderable("fake_fg", {});
  auto r2 = makeRenderable("fake_fg", {ShaderVariant{"HAS_NORMAL_MAP", true}});
  auto scene = makeSceneWithDefaultCamera(r1);
  scene->addRenderable(r2);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 2,
         "different variants keep two distinct PipelineBuildDesc");
}

void testMeshOverlayMaterialPassUsesLineListGeometry() {
  auto node = makeMeshOverlayRenderable();
  auto scene = makeSceneWithDefaultCamera(node);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto &items = fg.getPasses()[0].queue.getItems();
  EXPECT(items.size() == 1, "mesh overlay renderable should enter queue");
  if (items.empty()) {
    return;
  }

  const auto *indexBuffer =
      dynamic_cast<const IndexBuffer *>(&items[0].raster.indexBuffer.get());
  EXPECT(indexBuffer != nullptr, "mesh overlay item should keep index buffer");
  if (indexBuffer) {
    EXPECT(indexBuffer->getTopology() == PrimitiveTopology::LineList,
           "mesh overlay should draw derived line-list edges");
    EXPECT(indexBuffer->indexCount() == 6,
           "single triangle overlay should emit three line segments");
  }

  const auto passData = node->getValidatedPassData(Pass_Forward);
  EXPECT(passData.has_value(), "mesh overlay pass should stay validated");
  if (passData) {
    EXPECT(passData->get().objectSignature ==
               node->getPipelineSignature(Pass_Forward),
           "validated object signature should match public renderable "
           "signature");
  }

  const auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 1, "mesh overlay should produce one build desc");
  if (!infos.empty()) {
    EXPECT(infos[0].topology == PrimitiveTopology::LineList,
           "mesh overlay pipeline build desc should use line topology");
  }
}

void testFramePassNameIsStringID() {
  FramePass p{Pass_Forward, {}, {}};
  EXPECT(p.name == Pass_Forward, "FramePass.name is a StringID compared by id");
}

void testFrameGraphCompileAcceptsColorWriteThenSampleRead() {
  const auto offscreen =
      FrameGraphResourceRef::colorAttachment(StringID("hdr.color"));
  const auto swapColor =
      FrameGraphResourceRef::colorAttachment(StringID("swapchain.color"));

  FrameGraph graph;
  graph.addPass(
      FramePass{Pass_Forward,
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {FrameGraphWrite{offscreen}}});
  graph.addPass(FramePass{
      Pass_DebugOverlay,
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(offscreen.name)},
      {FrameGraphWrite{swapColor}}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(), "compile should accept write then sampled read");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
}

void testFrameGraphCompilePreservesSampledReadBindingName() {
  const auto shadowDepth =
      FrameGraphResourceRef::depthAttachment(StringID("shadow.main"));

  FrameGraph graph;
  graph.addPass(
      FramePass{Pass_Shadow,
                RenderTargetDesc::offscreenDepth(ImageFormat::D32Float),
                {},
                {},
                {FrameGraphWrite{shadowDepth}}});
  graph.addPass(FramePass{
      Pass_Forward,
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(shadowDepth.name, StringID("ShadowMap0"))},
      {}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(), "compile should accept shadow read");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
  if (compiled.getPasses().size() == 2 &&
      !compiled.getPasses()[1].reads.empty()) {
    EXPECT(compiled.getPasses()[1].reads[0].bindingName ==
               StringID("ShadowMap0"),
           "sampled read should preserve descriptor binding name");
  }
}

void testFrameGraphCompileAcceptsPostProcessSceneColorFlow() {
  FrameGraph graph;
  graph.addPass(
      FramePass{Pass_Forward,
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                {},
                {},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("hdr.color"))}}});
  graph.addPass(FramePass{
      Pass_PostProcess,
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(StringID("hdr.color"), StringID("SceneColor"))},
      {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
          StringID("swapchain.color"))}}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(),
         "compile should accept post-process pass after scene color");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
  if (compiled.getPasses().size() == 2) {
    EXPECT(compiled.getPasses()[1].name == Pass_PostProcess,
           "post-process pass identity should be preserved");
    EXPECT(compiled.getPasses()[1].reads[0].bindingName ==
               StringID("SceneColor"),
           "post-process sampled binding should be preserved");
    EXPECT(compiled.getPasses()[1].writes.size() == 1 &&
               compiled.getPasses()[1].writes[0].resource.name ==
                   StringID("swapchain.color") &&
               compiled.getPasses()[1].writes[0].resource.kind ==
                   FrameGraphAttachmentKind::Color,
           "post-process swapchain color write should be preserved");
  }
}

void testFrameGraphCompileAcceptsBloomPostProcessChain() {
  FrameGraph graph;
  GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  registry.registerResource("bloom.threshold");
  registry.registerResource("bloom.blurH");
  registry.registerResource("bloom.blur");

  const auto hdr =
      FrameGraphResourceRef::colorAttachment(StringID("hdr.color"));
  const auto threshold =
      FrameGraphResourceRef::colorAttachment(StringID("bloom.threshold"));
  const auto blurH =
      FrameGraphResourceRef::colorAttachment(StringID("bloom.blurH"));
  const auto blur =
      FrameGraphResourceRef::colorAttachment(StringID("bloom.blur"));
  const auto swapchain =
      FrameGraphResourceRef::colorAttachment(StringID("swapchain.color"));

  graph.addPass(
      FramePass{Pass_Forward,
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {FrameGraphWrite{hdr}}});
  graph.addPass(
      FramePass{Pass_BloomThreshold,
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {FrameGraphRead::sampled(hdr.name, StringID("SceneColor"))},
                {FrameGraphWrite{threshold}}});
  graph.addPass(FramePass{
      Pass_BloomBlurH,
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
      {},
      {FrameGraphRead::sampled(threshold.name, StringID("BloomSource"))},
      {FrameGraphWrite{blurH}}});
  graph.addPass(
      FramePass{Pass_BloomBlurV,
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {FrameGraphRead::sampled(blurH.name, StringID("BloomSource"))},
                {FrameGraphWrite{blur}}});
  graph.addPass(FramePass{
      Pass_PostProcess,
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(hdr.name, StringID("SceneColor")),
       FrameGraphRead::sampled(blur.name, StringID("BloomColor"))},
      {FrameGraphWrite{swapchain}}});

  auto compiled = graph.compile(registry);
  EXPECT(compiled.isValid(), "bloom post-process chain should compile");
  EXPECT(compiled.getPasses().size() == 5,
         "bloom post-process chain should preserve pass count");
  if (compiled.getPasses().size() == 5) {
    EXPECT(compiled.getPasses()[1].reads[0].bindingName ==
               StringID("SceneColor"),
           "BloomThreshold should read scene HDR as SceneColor");
    EXPECT(compiled.getPasses()[2].reads[0].bindingName ==
               StringID("BloomSource"),
           "BloomBlurH should read threshold as BloomSource");
    EXPECT(compiled.getPasses()[3].reads[0].bindingName ==
               StringID("BloomSource"),
           "BloomBlurV should read horizontal blur as BloomSource");
    EXPECT(compiled.getPasses()[4].reads[1].bindingName ==
               StringID("BloomColor"),
           "PostProcess should composite BloomColor");
  }
}

void testFrameGraphCompileSortsPassesBySourceTargetDag() {
  FrameGraph graph;
  graph.addPass(FramePass{
      StringID("ToneMapping"),
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(StringID("ldr.color"), StringID("SceneColor"))},
      {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
          StringID("swapchain.color"))}}});
  graph.addPass(
      FramePass{StringID("Forward"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {FrameGraphRead::sampled(StringID("camera.ubo")),
                 FrameGraphRead::sampled(StringID("geometry.vertex")),
                 FrameGraphRead::sampled(StringID("material.bsdf"))},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("hdr.color"))}}});
  graph.addPass(FramePass{
      StringID("Exposure"),
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
      {},
      {FrameGraphRead::sampled(StringID("hdr.color"), StringID("HdrColor"))},
      {FrameGraphWrite{
          FrameGraphResourceRef::colorAttachment(StringID("ldr.color"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 3, "compiled graph should keep all passes");
  if (passes.size() == 3) {
    EXPECT(passes[0].name == StringID("Forward"),
           "producer should run before hdr.color consumer");
    EXPECT(passes[0].sourcePassIndex == 1,
           "compiled producer should retain its original declaration index");
    EXPECT(passes[1].name == StringID("Exposure"),
           "middle pass should run after hdr.color producer");
    EXPECT(passes[1].sourcePassIndex == 2,
           "compiled middle pass should retain its original declaration index");
    EXPECT(passes[2].name == StringID("ToneMapping"),
           "swapchain writer should run last");
    EXPECT(passes[2].sourcePassIndex == 0,
           "compiled final pass should retain its original declaration index");
  }
}

void testFrameGraphCompileRejectsSelfProducedRead() {
  FrameGraph graph;
  graph.addPass(FramePass{
      StringID("Feedback"),
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
      {},
      {FrameGraphRead::sampled(StringID("hdr.color"), StringID("InputColor"))},
      {FrameGraphWrite{
          FrameGraphResourceRef::colorAttachment(StringID("hdr.color"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "compile should reject self-produced read");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("Feedback") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("hdr.color") != std::string::npos,
         "error should include resource name");
  EXPECT(errors.find("source has no producer") != std::string::npos,
         "self-produced read should report missing earlier producer");
}

void testFrameGraphCompileAllowsReadModifyWriteWithEarlierProducer() {
  const FrameGraphWrite blendHdr{
      FrameGraphResourceRef::colorAttachment(StringID("hdr.color")),
      std::string{"blend"}};

  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("OpaqueBase"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {FrameGraphRead::sampled(StringID("camera.ubo"))},
                {blendHdr}});
  graph.addPass(FramePass{
      StringID("TransparentBlend"),
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
      {},
      {FrameGraphRead::sampled(StringID("hdr.color"), StringID("SceneColor"))},
      {blendHdr}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled graph should keep both writers");
  if (passes.size() == 2) {
    EXPECT(passes[0].name == StringID("OpaqueBase"),
           "earlier hdr.color producer should run before read-modify-write "
           "pass");
    EXPECT(passes[1].name == StringID("TransparentBlend"),
           "read-modify-write pass should run after its earlier producer");
  }
}

void testFrameGraphCompileRejectsImportedWriteTarget() {
  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("BadImportedWriter"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("camera.ubo"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "compile should reject imported write target");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("BadImportedWriter") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("camera.ubo") != std::string::npos,
         "error should include imported target name");
  EXPECT(errors.find("imported") != std::string::npos,
         "error should explain target is imported/source-only");
}

void testFrameGraphCompileRejectsInvalidWriteMode() {
  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("BadWriteMode"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                                     StringID("hdr.color")),
                                 std::string{"overwrite-plus"}}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "compile should reject invalid writeMode");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("BadWriteMode") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("hdr.color") != std::string::npos,
         "error should include target name");
  EXPECT(errors.find("overwrite-plus") != std::string::npos,
         "error should include invalid writeMode");
}

void testFrameGraphCompileRejectsMixedDuplicateWriteMode() {
  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("BlendWriter"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                                     StringID("hdr.color")),
                                 std::string{"blend"}}}});
  graph.addPass(
      FramePass{StringID("AppendWriter"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                                     StringID("hdr.color")),
                                 std::string{"append"}}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(),
         "compile should reject mixed duplicate writeMode");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("AppendWriter") != std::string::npos,
         "error should include second writer pass name");
  EXPECT(errors.find("hdr.color") != std::string::npos,
         "error should include duplicate target name");
  EXPECT(errors.find("duplicate") != std::string::npos,
         "error should explain duplicate writer rejection");
}

void testFrameGraphCompileRejectsSamePassDuplicateTarget() {
  const FrameGraphWrite blendHdr{
      FrameGraphResourceRef::colorAttachment(StringID("hdr.color")),
      std::string{"blend"}};

  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("DuplicateInOnePass"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {blendHdr, blendHdr}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(),
         "compile should reject duplicate target entries within one pass");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("DuplicateInOnePass") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("hdr.color") != std::string::npos,
         "error should include duplicate target name");
  EXPECT(errors.find("within the same pass") != std::string::npos,
         "error should explain same-pass duplicate write");
}

void testFrameGraphCompileOrdersLegalDuplicateWritersByStableOrder() {
  FrameGraph graph;
  FramePass late{StringID("LateBlend"),
                 RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                 {},
                 {},
                 {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                                      StringID("hdr.color")),
                                  std::string{"blend"}}}};
  late.stableOrder = 20;
  FramePass early{StringID("EarlyBlend"),
                  RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                  {},
                  {},
                  {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                                       StringID("hdr.color")),
                                   std::string{"blend"}}}};
  early.stableOrder = 10;

  graph.addPass(late);
  graph.addPass(early);

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled graph should keep both writers");
  if (passes.size() == 2) {
    EXPECT(passes[0].name == StringID("EarlyBlend"),
           "legal duplicate writers should use stableOrder, not declaration "
           "order");
    EXPECT(passes[1].name == StringID("LateBlend"),
           "higher stableOrder duplicate writer should sort later");
  }
}

void testFrameGraphCompileReaderWaitsForAllLegalDuplicateWriters() {
  const FrameGraphWrite blendHdr{
      FrameGraphResourceRef::colorAttachment(StringID("hdr.color")),
      std::string{"blend"}};

  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("BaseHdr"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {blendHdr}});
  graph.addPass(FramePass{
      StringID("Composite"),
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
      {},
      {FrameGraphRead::sampled(StringID("hdr.color"), StringID("SceneColor"))},
      {FrameGraphWrite{
          FrameGraphResourceRef::colorAttachment(StringID("ldr.color"))}}});
  graph.addPass(
      FramePass{StringID("ExtraHdr"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {},
                {blendHdr}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 3, "compiled graph should keep all passes");
  if (passes.size() == 3) {
    EXPECT(passes[0].name == StringID("BaseHdr"),
           "first duplicate writer should be available before consumer");
    EXPECT(passes[1].name == StringID("ExtraHdr"),
           "reader should wait for all legal duplicate writers");
    EXPECT(passes[2].name == StringID("Composite"),
           "consumer should run after every hdr.color contributor");
  }
}

void testFrameGraphCompileRejectsKnownSourceWithoutProducer() {
  GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  registry.registerResource("custom.unwritten");

  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("KnownMissingSource"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                {},
                {FrameGraphRead::sampled(StringID("custom.unwritten"),
                                         StringID("MissingInput"))},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("ldr.color"))}}});

  const auto compiled = graph.compile(registry);
  EXPECT(!compiled.isValid(),
         "compile should reject known non-imported source without producer");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("KnownMissingSource") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("custom.unwritten") != std::string::npos,
         "error should include missing source name");
  EXPECT(errors.find("source has no producer") != std::string::npos,
         "known source without writer should report no producer");
}

void testFrameGraphCompileReportsUnknownSource() {
  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("UnknownSource"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                {},
                {FrameGraphRead::sampled(StringID("freeform.input"))},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("ldr.color"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "compile should reject unknown source");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("UnknownSource") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("freeform.input") != std::string::npos,
         "error should include unknown source name");
  EXPECT(errors.find("unknown source") != std::string::npos,
         "error should explain unknown source");
}

void testFrameGraphCompileReportsUnknownTarget() {
  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("UnknownTarget"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                {},
                {},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("freeform.output"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "compile should reject unknown target");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("UnknownTarget") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("freeform.output") != std::string::npos,
         "error should include unknown target name");
  EXPECT(errors.find("unknown target") != std::string::npos,
         "error should explain unknown target");
}

void testFrameGraphCompileReportsResourceCycle() {
  FrameGraph graph;
  graph.addPass(
      FramePass{StringID("ReadLdrWriteHdr"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                {},
                {FrameGraphRead::sampled(StringID("ldr.color"))},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("hdr.color"))}}});
  graph.addPass(
      FramePass{StringID("ReadHdrWriteLdr"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                {},
                {FrameGraphRead::sampled(StringID("hdr.color"))},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("ldr.color"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(),
         "compile should reject resource dependency cycle");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("cycle") != std::string::npos,
         "error should explain cycle");
  EXPECT(errors.find("ReadLdrWriteHdr") != std::string::npos,
         "cycle diagnostic should include first pass");
  EXPECT(errors.find("ReadHdrWriteLdr") != std::string::npos,
         "cycle diagnostic should include second pass");
}

void testFrameGraphCompileReportsReversePhaseDependencyCycle() {
  FramePass pre{StringID("PreReadsHdr"),
                RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                {},
                {FrameGraphRead::sampled(StringID("hdr.color"))},
                {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                    StringID("ldr.color"))}}};
  pre.phase = FrameGraphPhase::PreEffect;
  FramePass material{StringID("MaterialWritesHdr"),
                     RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                     {},
                     {},
                     {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                         StringID("hdr.color"))}}};
  material.phase = FrameGraphPhase::Material;

  FrameGraph graph;
  graph.addPass(pre);
  graph.addPass(material);

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(),
         "compile should reject dependency that contradicts phase order");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("cycle") != std::string::npos,
         "reverse phase dependency should produce a cycle diagnostic");
  EXPECT(errors.find("PreReadsHdr") != std::string::npos,
         "diagnostic should include pre-effect pass");
  EXPECT(errors.find("MaterialWritesHdr") != std::string::npos,
         "diagnostic should include material pass");
}

void testFrameGraphCompileOrdersIndependentPassesByPhase() {
  FrameGraph graph;
  FramePass debug{StringID("DebugPass"), {}, {}};
  debug.phase = FrameGraphPhase::Debug;
  FramePass post{StringID("PostPass"), {}, {}};
  post.phase = FrameGraphPhase::PostEffect;
  FramePass material{StringID("MaterialPass"), {}, {}};
  material.phase = FrameGraphPhase::Material;
  FramePass pre{StringID("PrePass"), {}, {}};
  pre.phase = FrameGraphPhase::PreEffect;

  graph.addPass(debug);
  graph.addPass(post);
  graph.addPass(material);
  graph.addPass(pre);

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 4, "compiled graph should keep all passes");
  EXPECT(passes[0].name == StringID("PrePass"),
         "pre effects should sort first");
  EXPECT(passes[1].name == StringID("MaterialPass"),
         "material passes should sort after pre effects");
  EXPECT(passes[2].name == StringID("PostPass"),
         "post effects should sort after material passes");
  EXPECT(passes[3].name == StringID("DebugPass"),
         "debug passes should sort after non-debug passes");
}

void testFrameGraphCompileOrdersSamePhaseByStableOrder() {
  FrameGraph graph;
  FramePass late{StringID("LatePass"), {}, {}};
  late.stableOrder = 20;
  FramePass early{StringID("EarlyPass"), {}, {}};
  early.stableOrder = 10;

  graph.addPass(late);
  graph.addPass(early);

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled graph should keep all passes");
  EXPECT(passes[0].name == StringID("EarlyPass"),
         "lower stableOrder should sort first within a phase");
  EXPECT(passes[1].name == StringID("LatePass"),
         "higher stableOrder should sort later within a phase");
}

void testFrameGraphCompileOrdersSamePhaseAndStableOrderByOriginalIndex() {
  FrameGraph graph;
  FramePass first{StringID("FirstPass"), {}, {}};
  first.stableOrder = 7;
  FramePass second{StringID("SecondPass"), {}, {}};
  second.stableOrder = 7;

  graph.addPass(first);
  graph.addPass(second);

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled graph should keep all passes");
  EXPECT(passes[0].name == StringID("FirstPass"),
         "original insertion index should break stableOrder ties");
  EXPECT(passes[1].name == StringID("SecondPass"),
         "later inserted pass should stay later when other keys tie");
}

void testFrameGraphBuildPlanConsumesRenderPathGraph() {
  const auto registry = GraphResourceRegistry::makeDefault();

  RenderPathGraph graphAsset;
  graphAsset.name = "ForwardMain";
  graphAsset.renderPath = RenderPath::Forward;
  graphAsset.features.push_back(RenderPathFeatureDependency{
      "shadow", "effects/shadow.render-feature.yaml"});
  graphAsset.passes.push_back(
      makeRenderPassNode("ToneMap", {"hdr.color"}, {"swapchain.color"}));
  graphAsset.passes.push_back(makeRenderPassNode(
      "ForwardOpaque",
      {"camera.ubo", "geometry.vertex", "material.bsdf", "feature.shadow"},
      {"hdr.color"}));

  const FrameGraph graph =
      buildFrameGraphFromRenderPathGraph(graphAsset, registry);
  const auto &declaredPasses = graph.getPasses();
  EXPECT(declaredPasses.size() == 2,
         "render path graph build should keep declared pass nodes");
  if (declaredPasses.size() == 2) {
    EXPECT(declaredPasses[0].name == StringID("ToneMap"),
           "declaration order should be retained before compile");
    EXPECT(declaredPasses[0].phase == FrameGraphPhase::Material,
           "render path graph pass should not be classified as legacy effect");
    EXPECT(declaredPasses[1].stableOrder == 1,
           "stable order should follow graph pass declaration order");
  }

  const auto compiled = graph.compile(registry);
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled graph should keep render path passes");
  if (passes.size() == 2) {
    EXPECT(passes[0].name == StringID("ForwardOpaque"),
           "DAG compile should place hdr.color producer first");
    EXPECT(passes[1].name == StringID("ToneMap"),
           "DAG compile should place hdr.color consumer second");
  }
}

void testFrameGraphBuildPlanPreservesRenderPathGraphMetadata() {
  const auto registry = GraphResourceRegistry::makeDefault();

  RenderPassNode node = makeRenderPassNode(
      "PostProcess", {"hdr.color", "feature.toneMapping"}, {"swapchain.color"});
  node.shaderUri = "post_process";
  node.dispatch = RenderPassDispatch::Fullscreen;
  node.filters.renderClasses = {"fullscreen.post"};
  node.filters.bsdfTypes = {"none"};
  node.renderState.cullMode = CullMode::None;
  node.renderState.depthTestEnable = false;
  node.renderState.depthWriteEnable = false;
  node.renderState.depthOp = CompareOp::Always;
  node.renderState.blendEnable = false;

  RenderPathGraph graphAsset;
  graphAsset.name = "MetadataGraph";
  graphAsset.renderPath = RenderPath::Forward;
  graphAsset.passes.push_back(makeRenderPassNode(
      "Forward", {"scene.camera", "geometry.vertex", "material.bsdf"},
      {"hdr.color"}));
  graphAsset.passes.push_back(std::move(node));

  const FrameGraph graph =
      buildFrameGraphFromRenderPathGraph(graphAsset, registry);
  const auto &declaredPasses = graph.getPasses();
  EXPECT(declaredPasses.size() == 2,
         "render path graph build should keep producer and metadata pass");
  if (declaredPasses.size() < 2) {
    return;
  }

  const FramePass &pass = declaredPasses[1];
  EXPECT(pass.shaderUri == LX_core::ResourceUri("post_process"),
         "FramePass should preserve graph shader uri");
  EXPECT(pass.stage == RenderPassStage::Raster,
         "FramePass should preserve graph stage");
  EXPECT(pass.dispatch == RenderPassDispatch::Fullscreen,
         "FramePass should preserve graph dispatch");
  EXPECT(pass.filters.renderClasses.size() == 1 &&
             pass.filters.renderClasses[0] == "fullscreen.post",
         "FramePass should preserve render class filters");
  EXPECT(pass.filters.bsdfTypes.size() == 1 &&
             pass.filters.bsdfTypes[0] == "none",
         "FramePass should preserve bsdf filters");
  EXPECT(pass.renderState.cullMode == CullMode::None,
         "FramePass should preserve cull mode");
  EXPECT(!pass.renderState.depthTestEnable,
         "FramePass should preserve depth test state");
  EXPECT(!pass.renderState.depthWriteEnable,
         "FramePass should preserve depth write state");
  EXPECT(pass.renderState.depthOp == CompareOp::Always,
         "FramePass should preserve depth compare op");
}

void testFrameGraphBuildPlanRejectsIncompleteRenderPathPass() {
  const auto registry = GraphResourceRegistry::makeDefault();

  RenderPathGraph missingShader;
  missingShader.name = "MissingShader";
  missingShader.passes.push_back(
      makeRenderPassNode("ForwardOpaque", {"scene.camera"}, {"hdr.color"}));
  missingShader.passes[0].shaderUri = LX_core::ResourceUri("");
  EXPECT(throwsInvalidArgument(
             [&] {
               [[maybe_unused]] const FrameGraph graph =
                   buildFrameGraphFromRenderPathGraph(missingShader, registry);
             },
             "shader"),
         "render path graph pass without shader should fail fast");

  RenderPathGraph missingSources;
  missingSources.name = "MissingSources";
  missingSources.passes.push_back(
      makeRenderPassNode("ForwardOpaque", {}, {"hdr.color"}));
  EXPECT(throwsInvalidArgument(
             [&] {
               [[maybe_unused]] const FrameGraph graph =
                   buildFrameGraphFromRenderPathGraph(missingSources, registry);
             },
             "sources"),
         "render path graph pass without sources should fail fast");

  RenderPathGraph missingTargets;
  missingTargets.name = "MissingTargets";
  missingTargets.passes.push_back(
      makeRenderPassNode("ForwardOpaque", {"scene.camera"}, {}));
  EXPECT(throwsInvalidArgument(
             [&] {
               [[maybe_unused]] const FrameGraph graph =
                   buildFrameGraphFromRenderPathGraph(missingTargets, registry);
             },
             "targets"),
         "render path graph pass without targets should fail fast");
}

void testDefaultForwardRenderPathGraphPassSetValidation() {
  RenderPathGraph valid;
  valid.name = "DefaultForward";
  valid.renderPath = RenderPath::Forward;
  valid.passes.push_back(
      makeRenderPassNode("Forward", {"scene.camera"}, {"hdr.color"}));
  valid.passes.push_back(
      makeRenderPassNode("PostProcess", {"hdr.color"}, {"swapchain.color"}));

  validateRenderPathGraphPassSet(valid, {Pass_Forward, Pass_PostProcess},
                                 {Pass_Forward, Pass_PostProcess});

  RenderPathGraph unknown = valid;
  unknown.passes.push_back(
      makeRenderPassNode("TypoPass", {"hdr.color"}, {"swapchain.color"}));
  EXPECT(throwsInvalidArgument(
             [&] {
               validateRenderPathGraphPassSet(unknown,
                                              {Pass_Forward, Pass_PostProcess},
                                              {Pass_Forward, Pass_PostProcess});
             },
             "unsupported pass"),
         "default forward graph should reject unknown pass ids");

  RenderPathGraph missing = valid;
  missing.passes.erase(missing.passes.begin());
  EXPECT(throwsInvalidArgument(
             [&] {
               validateRenderPathGraphPassSet(missing,
                                              {Pass_Forward, Pass_PostProcess},
                                              {Pass_Forward, Pass_PostProcess});
             },
             "missing required pass"),
         "default forward graph should reject missing required pass ids");

  RenderPathGraph duplicate = valid;
  duplicate.passes.push_back(
      makeRenderPassNode("Forward", {"scene.camera"}, {"hdr.color"}));
  EXPECT(throwsInvalidArgument(
             [&] {
               validateRenderPathGraphPassSet(duplicate,
                                              {Pass_Forward, Pass_PostProcess},
                                              {Pass_Forward, Pass_PostProcess});
             },
             "duplicate pass"),
         "default forward graph should reject duplicate pass ids");
}

void testDefaultRenderPathGraphOrderComesFromSourceTargetDag() {
  const auto registry = GraphResourceRegistry::makeDefault();

  RenderPathGraph graphAsset;
  graphAsset.name = "DefaultForward";
  graphAsset.renderPath = RenderPath::Forward;
  graphAsset.passes.push_back(makeRenderPassNode(
      GlobalStringTable::get().toDebugString(Pass_PostProcess), {"hdr.color"},
      {"swapchain.color"}));
  graphAsset.passes.push_back(makeRenderPassNode(
      GlobalStringTable::get().toDebugString(Pass_Forward),
      {"camera.ubo", "geometry.vertex", "material.bsdf"}, {"hdr.color"}));

  const FrameGraph graph =
      buildFrameGraphFromRenderPathGraph(graphAsset, registry);
  const auto &declaredPasses = graph.getPasses();
  EXPECT(declaredPasses.size() == 2,
         "default render path graph should keep both declared passes");
  if (declaredPasses.size() == 2) {
    EXPECT(declaredPasses[0].name == Pass_PostProcess,
           "test intentionally declares PostProcess before Forward");
    EXPECT(declaredPasses[1].name == Pass_Forward,
           "test intentionally declares Forward after PostProcess");
  }

  const auto compiled = graph.compile(registry);
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled graph should keep both passes");
  if (passes.size() == 2) {
    EXPECT(passes[0].name == Pass_Forward,
           "hdr.color producer should execute before PostProcess");
    EXPECT(passes[0].sourcePassIndex == 1,
           "compiled Forward should point back to its declared queue");
    EXPECT(passes[1].name == Pass_PostProcess,
           "PostProcess should execute after its hdr.color source exists");
    EXPECT(passes[1].sourcePassIndex == 0,
           "compiled PostProcess should point back to its declared queue");
  }
}

void testDeferredLightingReadsComeFromRenderPathGraphSources() {
  const auto registry = GraphResourceRegistry::makeDefault();

  RenderPathGraph graphAsset;
  graphAsset.name = "DeferredMainFixture";
  graphAsset.renderPath = RenderPath::Deferred;
  graphAsset.passes.push_back(
      makeRenderPassNode(GlobalStringTable::get().toDebugString(Pass_Deferred),
                         {"scene.camera", "geometry.vertex", "material.bsdf"},
                         {"gbuffer.emissive", "depth.main"}));
  RenderPassNode lighting = makeRenderPassNode(
      GlobalStringTable::get().toDebugString(Pass_DeferredLighting),
      {"gbuffer.emissive", "depth.main"}, {"hdr.color"});
  lighting.dispatch = RenderPassDispatch::Fullscreen;
  graphAsset.passes.push_back(std::move(lighting));

  const FrameGraph graph =
      buildFrameGraphFromRenderPathGraph(graphAsset, registry);
  const auto &declaredPasses = graph.getPasses();
  EXPECT(declaredPasses.size() == 2,
         "deferred fixture graph should keep both graph-declared passes");
  if (declaredPasses.size() == 2) {
    const FramePass &lightingPass = declaredPasses[1];
    EXPECT(lightingPass.name == Pass_DeferredLighting,
           "lighting pass should keep the graph pass identity");
    EXPECT(lightingPass.reads.size() == 2,
           "lighting pass should keep exactly the graph-declared sources");
    if (lightingPass.reads.size() == 2) {
      EXPECT(lightingPass.reads[0].resource == StringID("gbuffer.emissive"),
             "changed DeferredLighting source should survive graph build");
      EXPECT(lightingPass.reads[1].resource == StringID("depth.main"),
             "depth source should survive graph build");
    }
  }

  const auto compiled = graph.compile(registry);
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 2, "compiled deferred graph should keep both passes");
  if (passes.size() == 2) {
    const CompiledFrameGraphPass &lightingPass = passes[1];
    EXPECT(lightingPass.name == Pass_DeferredLighting,
           "DeferredLighting should execute after its G-buffer producer");
    EXPECT(lightingPass.reads.size() == 2,
           "compiled DeferredLighting reads should still come from graph "
           "sources");
    if (lightingPass.reads.size() == 2) {
      EXPECT(lightingPass.reads[0].resource == StringID("gbuffer.emissive"),
             "compiled graph must not replace the changed lighting source");
    }
  }
}

void testDirectionalLightCascadeSplitsUpdateFromCamera() {
  auto cameraNode = SceneNode::create("csm_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  camera->get().setNearPlane(0.5f);
  camera->get().setFarPlane(120.0f);
  camera->get().setFovY(60.0f);
  camera->get().setAspect(1.5f);
  cameraNode->setTranslation({0.0f, 2.0f, 8.0f});
  camera->get().lookAt({0.0f, 2.0f, 8.0f}, {0.0f, 0.0f, 0.0f},
                       {0.0f, 1.0f, 0.0f});

  DirectionalLight light;
  light.setShadowCascadeCount(4);
  light.setShadowDistance(80.0f);
  light.updateShadowCascadesForCamera(camera->get(), 0.5f);

  const auto splits = light.getCascadeSplits();
  EXPECT(light.getShadowCascadeCount() == 4, "cascade count should be 4");
  EXPECT(splits.x > camera->get().getNearPlane(), "split 0 after near");
  EXPECT(splits.x < splits.y && splits.y < splits.z && splits.z < splits.w,
         "cascade splits should be strictly increasing");
  EXPECT(splits.w <= 80.001f, "last split should respect shadow distance");

  const auto firstSplit = splits.x;
  camera->get().setNearPlane(2.0f);
  light.updateShadowCascadesForCamera(camera->get(), 0.5f);
  EXPECT(light.getCascadeSplits().x != firstSplit,
         "cascade split should update when camera near plane changes");
}

void testDirectionalLightPendingDirectionUpdatesUboBeforeAttach() {
  DirectionalLight light;
  const Vec3f requested{-0.35f, -1.0f, -0.25f};
  const Vec3f expected = requested.normalized();

  light.setDirection(requested);

  const Vec3f direction = light.getDirection();
  EXPECT(approx(direction.x, expected.x, 0.001f) &&
             approx(direction.y, expected.y, 0.001f) &&
             approx(direction.z, expected.z, 0.001f),
         "unattached directional light should report pending direction");
  const auto &ubo = light.getDirectionalUBO();
  EXPECT(approx(ubo.param.dir.x, expected.x, 0.001f) &&
             approx(ubo.param.dir.y, expected.y, 0.001f) &&
             approx(ubo.param.dir.z, expected.z, 0.001f),
         "unattached directional light UBO should store pending direction");
}

void testDirectionalShadowDebugViewRecreatesCascadeMatrix() {
  auto cameraNode = SceneNode::create("shadow_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  camera->get().setNearPlane(0.5f);
  camera->get().setFarPlane(100.0f);
  camera->get().setFovY(55.0f);
  camera->get().setAspect(1.7f);
  camera->get().lookAt({4.0f, 3.0f, 9.0f}, {0.0f, 0.7f, 0.0f},
                       {0.0f, 1.0f, 0.0f});

  DirectionalLight light;
  light.setShadowCascadeCount(1);
  light.setShadowDistance(80.0f);
  light.updateShadowCascadesForCamera(camera->get(), 0.5f);

  const auto debugView = light.getShadowCascadeDebugView(0);
  EXPECT(debugView.has_value(), "cascade debug view should be available");
  if (!debugView.has_value()) {
    return;
  }

  auto debugCameraNode = SceneNode::create("shadow_debug_camera");
  auto debugCamera = debugCameraNode->addComponent<CameraComponent>();
  debugCamera->get().applyProjectionState(
      CameraType::Orthographic, 45.0f, 1.0f, debugView->nearPlane,
      debugView->farPlane, debugView->left, debugView->right, debugView->bottom,
      debugView->top);
  debugCamera->get().lookAt(debugView->eye, debugView->target, debugView->up);

  const Mat4f debugViewProj =
      debugCamera->get().getProjMatrix(0.0f, GraphicsAPI::DirectX) *
      debugCamera->get().getViewMatrix();
  EXPECT(approxMatrix(debugViewProj,
                      light.getDirectionalUBO().param.cascadeViewProj[0]),
         "debug light-view camera must recreate the shadow cascade matrix");
}

void testDirectionalShadowCascadeStoresLightDepthRange() {
  auto cameraNode = SceneNode::create("shadow_range_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  camera->get().setNearPlane(0.5f);
  camera->get().setFarPlane(100.0f);
  camera->get().setFovY(55.0f);
  camera->get().setAspect(1.7f);
  camera->get().lookAt({4.0f, 3.0f, 9.0f}, {0.0f, 0.7f, 0.0f},
                       {0.0f, 1.0f, 0.0f});

  DirectionalLight light;
  light.setShadowCascadeCount(1);
  light.setShadowDistance(80.0f);
  light.updateShadowCascadesForCamera(camera->get(), 0.5f);

  const auto debugView = light.getShadowCascadeDebugView(0);
  EXPECT(debugView.has_value(), "cascade debug view should be available");
  if (!debugView.has_value()) {
    return;
  }

  const float expectedRange =
      std::abs(debugView->farPlane - debugView->nearPlane);
  const float storedRange =
      light.getDirectionalUBO().param.cascadeDepthRanges.x;
  EXPECT(approx(storedRange, expectedRange, 0.001f),
         "cascade depth range should match the light-view depth span");
}

void testDirectionalShadowCascadeUboSnapshotIsStable() {
  auto cameraNode = SceneNode::create("shadow_snapshot_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  camera->get().setNearPlane(0.5f);
  camera->get().setFarPlane(100.0f);
  camera->get().setFovY(55.0f);
  camera->get().setAspect(1.7f);
  camera->get().lookAt({4.0f, 3.0f, 9.0f}, {0.0f, 0.7f, 0.0f},
                       {0.0f, 1.0f, 0.0f});

  DirectionalLight light;
  light.setShadowCascadeCount(4);
  light.setShadowDistance(80.0f);
  light.updateShadowCascadesForCamera(camera->get(), 0.5f);

  const auto cascade0 = light.makeShadowCascadeUBOSnapshot(0);
  const auto cascade3 = light.makeShadowCascadeUBOSnapshot(3);

  EXPECT(cascade0 != nullptr, "cascade 0 snapshot should exist");
  EXPECT(cascade3 != nullptr, "cascade 3 snapshot should exist");
  EXPECT(cascade0->getBindingName() == StringID("LightUBO"),
         "snapshot should bind as LightUBO for shadow shaders");
  EXPECT(cascade0->getBackendCacheIdentity() !=
             light.getDirectionalUBO().getBackendCacheIdentity(),
         "snapshot should not overwrite the main directional light UBO");
  EXPECT(cascade0->getBackendCacheIdentity() !=
             cascade3->getBackendCacheIdentity(),
         "each cascade snapshot needs an independent GPU buffer");
  EXPECT(approxMatrix(cascade0->param.shadowViewProj,
                      light.getDirectionalUBO().param.cascadeViewProj[0]),
         "cascade 0 snapshot should use cascade 0 matrix");
  EXPECT(approxMatrix(cascade3->param.shadowViewProj,
                      light.getDirectionalUBO().param.cascadeViewProj[3]),
         "cascade 3 snapshot should use cascade 3 matrix");

  const Mat4f stableCascade0 = cascade0->param.shadowViewProj;
  light.setActiveShadowCascade(3);
  EXPECT(approxMatrix(cascade0->param.shadowViewProj, stableCascade0),
         "snapshot must remain stable after active cascade changes");
}

void testFrameGraphCompilePreservesTargetDescriptions() {
  auto offscreenColor = RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);
  offscreenColor.sampleCount = 2;
  offscreenColor.layerCount = 3;

  auto depthOnly = RenderTargetDesc::offscreenDepth(ImageFormat::D32Float);
  depthOnly.sampleCount = 4;
  depthOnly.layerCount = 6;

  FrameGraph graph;
  graph.addPass(FramePass{Pass_Forward, offscreenColor, {}});
  graph.addPass(FramePass{Pass_Shadow, depthOnly, {}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(), "compile should accept target-only passes");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
  if (compiled.getPasses().size() == 2) {
    const auto &compiledColor = compiled.getPasses()[0].target;
    EXPECT(compiledColor.role == RenderTargetRole::Offscreen,
           "offscreen color role should be preserved");
    EXPECT(compiledColor.colorFormat == ImageFormat::RGBA8,
           "offscreen color format should be preserved");
    EXPECT(!compiledColor.depthFormat.has_value(),
           "offscreen color target should preserve null depth attachment");
    EXPECT(compiledColor.sampleCount == 2,
           "offscreen color sample count should be preserved");
    EXPECT(compiledColor.layerCount == 3,
           "offscreen color layer count should be preserved");

    const auto &compiledDepth = compiled.getPasses()[1].target;
    EXPECT(compiledDepth.role == RenderTargetRole::Offscreen,
           "depth-only role should be preserved");
    EXPECT(!compiledDepth.colorFormat.has_value(),
           "depth-only target should preserve null color attachment");
    EXPECT(compiledDepth.depthFormat == ImageFormat::D32Float,
           "depth-only format should be preserved");
    EXPECT(compiledDepth.sampleCount == 4,
           "depth-only sample count should be preserved");
    EXPECT(compiledDepth.layerCount == 6,
           "depth-only layer count should be preserved");
  }
}

void testRenderTargetToDescUsesMutatedLegacyFields() {
  RenderTarget target;
  target.colorFormat = ImageFormat::RGBA8;
  target.depthFormat = ImageFormat::D24UnormS8;

  const auto desc = target.toDesc();
  EXPECT(desc.colorFormat == ImageFormat::RGBA8,
         "toDesc should preserve mutated legacy colorFormat");
  EXPECT(desc.depthFormat == ImageFormat::D24UnormS8,
         "toDesc should preserve mutated legacy depthFormat");
}

void testRenderTargetDescPreservesMultipleColorFormats() {
  auto gbuffer = RenderTargetDesc::offscreenColors(
      {ImageFormat::RGBA16Float, ImageFormat::RGBA8}, ImageFormat::D32Float);

  EXPECT(gbuffer.colorFormat == ImageFormat::RGBA16Float,
         "first color format remains the legacy colorFormat");
  EXPECT(gbuffer.colorAttachmentCount() == 2,
         "MRT target should expose two color attachments");
  EXPECT(gbuffer.getColorFormats()[0] == ImageFormat::RGBA16Float,
         "MRT color 0 should be preserved");
  EXPECT(gbuffer.getColorFormats()[1] == ImageFormat::RGBA8,
         "MRT color 1 should be preserved");

  auto single = RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  single.depthFormat = ImageFormat::D32Float;
  EXPECT(gbuffer != single, "MRT target should not equal single-color target");
  EXPECT(gbuffer.getPipelineSignature() != single.getPipelineSignature(),
         "MRT target signature should include all color formats");

  RenderTarget target{gbuffer};
  const auto roundTrip = target.toDesc();
  EXPECT(roundTrip.getColorFormats() == gbuffer.getColorFormats(),
         "RenderTarget compatibility shell should preserve MRT colors");
}

void testFrameGraphCompileReportsMissingRead() {
  FrameGraph graph;
  graph.addPass(FramePass{
      Pass_Forward,
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(StringID("missing.depth"))},
      {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
          StringID("swapchain.color"))}}});

  const auto compiled = graph.compile();
  EXPECT(!compiled.isValid(), "compile should reject missing resource read");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("Forward") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("missing.depth") != std::string::npos,
         "error should include resource name");
}

void testFrameGraphCompileReportsDuplicateWrite() {
  const auto duplicate =
      FrameGraphResourceRef::colorAttachment(StringID("hdr.color"));

  FrameGraph graph;
  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                          {},
                          {},
                          {FrameGraphWrite{duplicate}}});
  graph.addPass(FramePass{Pass_DebugOverlay,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                          {},
                          {},
                          {FrameGraphWrite{duplicate}}});

  const auto compiled = graph.compile();
  EXPECT(!compiled.isValid(), "compile should reject duplicate resource write");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("DebugOverlay") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("hdr.color") != std::string::npos,
         "error should include resource name");
  EXPECT(errors.find("duplicate") != std::string::npos,
         "error should include duplicate write reason");
}

void testFrameGraphCompileReportsUnnamedWrite() {
  FrameGraph graph;
  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                          {},
                          {},
                          {FrameGraphWrite{FrameGraphResourceRef{
                              StringID{}, FrameGraphAttachmentKind::Color}}}});

  const auto compiled = graph.compile();
  EXPECT(!compiled.isValid(), "compile should reject unnamed resource write");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("Forward") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("unnamed resource") != std::string::npos,
         "error should include unnamed write reason");
}

void testBuildFromSceneIsIdempotent() {
  auto r = makeRenderable();
  auto scene = makeSceneWithDefaultCamera(r);
  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});

  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));
  fg.build(LX_core::RenderWorkBuildContext::realtime(
      *scene)); // second call should clear + refill, not accumulate

  EXPECT(fg.getPasses()[0].queue.getItems().size() == 1,
         "build clears previous items on re-entry");
}

void testPassFilterExcludesNonMatching() {
  // Renderable A participates in Forward + Shadow; B only in Forward.
  auto rA = makeRenderable("fake_fg_a", {}, true);
  auto rB = makeRenderable("fake_fg_b");
  auto scene = makeSceneWithDefaultCamera(rA);
  scene->addRenderable(rB);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.addPass(FramePass{Pass_Shadow, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto &passes = fg.getPasses();
  EXPECT(passes.size() == 2, "two passes configured");
  EXPECT(passes[0].queue.getItems().size() == 2,
         "Forward pass: both renderables match");
  EXPECT(passes[1].queue.getItems().size() == 1,
         "Shadow pass: only rA supports shadow");
}

void testForwardQueueDrawsOpaqueBeforeTransparentAndSortsTransparentBackToFront() {
  auto opaque = makeRenderable("opaque_body");
  opaque->setTranslation({0.0f, 0.0f, -2.0f});
  auto nearGlass = makeTransparentRenderable("near_glass", {0.0f, 0.0f, -2.0f});
  auto farGlass = makeTransparentRenderable("far_glass", {0.0f, 0.0f, -8.0f});

  auto scene = Scene::create("transparent_sort");
  scene->addRenderable(nearGlass);
  scene->addRenderable(opaque);
  scene->addRenderable(farGlass);

  scene->addCamera(makeCameraWithTargetAndMask(RenderTarget{}, Layer_All));

  RenderWorkQueue queue;
  queue.build(RenderWorkBuildContext::realtime(*scene), Pass_Forward,
              RenderTarget{});

  const auto &items = queue.getItems();
  EXPECT(items.size() == 3, "opaque and both transparent items should render");
  if (items.size() != 3) {
    return;
  }

  EXPECT(items[0].debugId == opaque->getDebugId(),
         "opaque item should draw before transparent items");
  EXPECT(items[1].debugId == farGlass->getDebugId(),
         "far transparent item should draw before near transparent item");
  EXPECT(items[2].debugId == nearGlass->getDebugId(),
         "near transparent item should draw last");
}

void testShadowQueueUsesFallbackVisibilityWhenNoShadowCamera() {
  auto rA = makeRenderable("fake_fg_shadow_a", {}, true);
  auto scene = Scene::create(rA);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Shadow,
                       RenderTargetDesc::offscreenDepth(ImageFormat::D32Float),
                       {},
                       {},
                       {FrameGraphWrite{FrameGraphResourceRef::depthAttachment(
                           StringID("shadow.depth"))}}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  EXPECT(fg.getPasses()[0].queue.getItems().size() == 1,
         "Shadow pass should include caster even without a target-matching "
         "camera");
}

void testMultiCameraTargetFilter() {
  // REQ-009: camera filtered by target in getSceneLevelResources.
  const RenderTarget targetA{ImageFormat::BGRA8, ImageFormat::D32Float, 2};
  const RenderTarget targetB{ImageFormat::BGRA8, ImageFormat::D32Float, 4};

  auto camA = makeCameraWithTarget(targetA);
  auto camB = makeCameraWithTarget(targetB);

  auto scene = Scene::create(makeRenderable());
  scene->addCamera(camA);
  scene->addCamera(camB);

  auto resA = scene->getSceneLevelResources(Pass_Forward, targetA);
  EXPECT(countResourcesNamed(resA, StringID("CameraUBO")) == 1,
         "Forward targetA: exactly camA UBO");

  // For targetB: only camB.
  auto resB = scene->getSceneLevelResources(Pass_Forward, targetB);
  EXPECT(countResourcesNamed(resB, StringID("CameraUBO")) == 1,
         "Forward targetB: exactly camB UBO");

  // Cross-check camera UBO identity: scene resources come from
  // SceneResourceTable handles, not from CameraComponent-owned compatibility
  // data.
  const auto resACamera = findResourceNamed(resA, StringID("CameraUBO"));
  const auto resBCamera = findResourceNamed(resB, StringID("CameraUBO"));
  if (resACamera.has_value() && resBCamera.has_value()) {
    const auto camAComponent = camA->getComponent<CameraComponent>();
    const auto camBComponent = camB->getComponent<CameraComponent>();
    const auto camAUbo = scene->resources().getCameraUboResource(
        camAComponent->get().getCameraHandle());
    const auto camBUbo = scene->resources().getCameraUboResource(
        camBComponent->get().getCameraHandle());
    EXPECT(resACamera->get().isResource() && camAUbo.isValid() &&
               resACamera->get().resource().getBackendCacheIdentity() ==
                   camAUbo.getBackendCacheIdentity(),
           "resA CameraUBO is camA's table-owned UBO");
    EXPECT(resBCamera->get().isResource() && camBUbo.isValid() &&
               resBCamera->get().resource().getBackendCacheIdentity() ==
                   camBUbo.getBackendCacheIdentity(),
           "resB CameraUBO is camB's table-owned UBO");
  }
}

void testOffscreenDepthTargetDoesNotMatchDefaultCamera() {
  auto scene = makeSceneWithDefaultCamera(makeRenderable());

  const auto depthOnly =
      RenderTarget{RenderTargetDesc::offscreenDepth(ImageFormat::D32Float)};
  const auto resources = scene->getSceneLevelResources(Pass_Forward, depthOnly);

  EXPECT(countResourcesNamed(resources, StringID("CameraUBO")) == 0,
         "offscreen depth target excludes default swapchain camera resources");
}

void testMultiLightPassFilter() {
  // REQ-009: light filtered by supportsPass.
  auto lightForward = makeLightWithPasses({Pass_Forward});
  auto lightShadow = makeLightWithPasses({Pass_Shadow});
  auto lightBoth = makeLightWithPasses({Pass_Forward, Pass_Shadow});

  auto scene = Scene::create(makeRenderable());
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  attachLightNode(scene, lightForward, "forward_light");
  attachLightNode(scene, lightShadow, "shadow_light");
  attachLightNode(scene, lightBoth, "both_light");

  auto resForward = scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
  EXPECT(countResourcesNamed(resForward, StringID("CameraUBO")) == 1,
         "Pass_Forward: 1 matching camera UBO");
  EXPECT(countResourcesNamed(resForward, StringID("LightUBO")) == 2,
         "Pass_Forward: 2 matching LightUBOs");
  EXPECT(countResourcesNamed(resForward, StringID("SceneLightsUBO")) == 1,
         "Pass_Forward: SceneLightsUBO");

  auto resShadow = scene->getSceneLevelResources(Pass_Shadow, RenderTarget{});
  EXPECT(countResourcesNamed(resShadow, StringID("CameraUBO")) == 1,
         "Pass_Shadow: 1 matching camera UBO");
  EXPECT(countResourcesNamed(resShadow, StringID("LightUBO")) == 2,
         "Pass_Shadow: 2 matching LightUBOs");
  EXPECT(countResourcesNamed(resShadow, StringID("SceneLightsUBO")) == 1,
         "Pass_Shadow: SceneLightsUBO");
}

void testNullOptCameraBeforeAndAfterFill() {
  // REQ-009: a camera with nullopt target never matches. After setTarget it
  // matches exactly that target.
  const RenderTarget customTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 3};

  auto testCam = makeCameraNoTarget();
  auto scene = Scene::create(makeRenderable());
  scene->addCamera(testCam);

  auto resBefore = scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(countResourcesNamed(resBefore, StringID("CameraUBO")) == 0,
         "nullopt camera excludes camera and Scene has no hidden light");

  testCam->getComponent<CameraComponent>()->get().setTarget(customTarget);

  auto resAfter = scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(countResourcesNamed(resAfter, StringID("CameraUBO")) == 1,
         "after setTarget(customTarget): camera UBO");

  EXPECT(testCam->getComponent<CameraComponent>()->get().matchesTarget(
             customTarget),
         "testCam->matchesTarget(customTarget) after setTarget");
}

void testMultiPassRebuildIsIdempotent() {
  auto rA = makeRenderable("fake_fg_a", {}, true);
  auto rB = makeRenderable("fake_fg_b");
  auto scene = makeSceneWithDefaultCamera(rA);
  scene->addRenderable(rB);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.addPass(FramePass{Pass_Shadow, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));
  fg.build(LX_core::RenderWorkBuildContext::realtime(
      *scene)); // second call must clear + refill, not accumulate.

  const auto &passes = fg.getPasses();
  EXPECT(passes[0].queue.getItems().size() == 2,
         "Forward pass still has 2 items after rebuild");
  EXPECT(passes[1].queue.getItems().size() == 1,
         "Shadow pass still has 1 item after rebuild");
}

void testCollectAcrossMultiplePasses() {
  auto r = makeRenderable();
  auto scene = makeSceneWithDefaultCamera(r);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  // Same pass name repeated would produce identical PipelineKey for the same
  // template; exercise the cross-pass dedup path by adding the same pass twice.
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 1,
         "same pipeline key across two passes dedupes to 1 info");
}

void testFrameGraphKeepsDifferentTargetsAsDifferentBuildDescs() {
  auto r = makeRenderable();
  auto scene = Scene::create(r);

  const auto targetA =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  const auto targetB = RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);

  scene->addCamera(makeCameraWithTarget(RenderTarget{targetA}));
  scene->addCamera(makeCameraWithTarget(RenderTarget{targetB}));

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, targetA, {}});
  fg.addPass(FramePass{Pass_Forward, targetB, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(
      infos.size() == 2,
      "same object/material on different targets should keep two build descs");
}

void testFrameGraphDedupesExactSameTargetBuildDescs() {
  auto r = makeRenderable();
  auto scene = Scene::create(r);

  auto target =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  target.sampleCount = 4;
  scene->addCamera(makeCameraWithTarget(RenderTarget{target}));

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 1,
         "same object/material on exact same target should dedupe");
}

void testVisibilityMaskFiltersRenderables() {
  const RenderTarget target{ImageFormat::BGRA8, ImageFormat::D32Float, 2};

  auto visible = makeRenderableWithMask(0x1u, "visible_fg");
  auto hidden = makeRenderableWithMask(0x2u, "hidden_fg");
  auto scene = Scene::create(visible);
  scene->addRenderable(hidden);
  scene->addCamera(makeCameraWithTargetAndMask(target, 0x1u));

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto &items = fg.getPasses()[0].queue.getItems();
  EXPECT(items.size() == 1, "only mask-visible renderable enters queue");
  if (items.size() == 1) {
    const auto &validated = visible->getValidatedPassData(Pass_Forward)->get();
    EXPECT(items[0].objectSignature == validated.objectSignature,
           "visible renderable survives mask culling");
  }
}

void testVisibilityMaskOrsMatchingCameraMasks() {
  const RenderTarget target{ImageFormat::BGRA8, ImageFormat::D32Float, 3};

  auto layer1 = makeRenderableWithMask(0x1u, "mask_or_a");
  auto layer2 = makeRenderableWithMask(0x2u, "mask_or_b");
  auto scene = Scene::create(layer1);
  scene->addRenderable(layer2);
  scene->addCamera(makeCameraWithTargetAndMask(target, 0x1u));
  scene->addCamera(makeCameraWithTargetAndMask(target, 0x2u));

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  EXPECT(fg.getPasses()[0].queue.getItems().size() == 2,
         "matching cameras OR culling masks before renderable filtering");
}

void testVisibilityFilteringKeepsSceneResources() {
  const RenderTarget target{ImageFormat::BGRA8, ImageFormat::D32Float, 5};

  auto visible = makeRenderableWithMask(0x1u, "resource_visible");
  auto hidden = makeRenderableWithMask(0x2u, "resource_hidden");
  auto scene = Scene::create(visible);
  scene->addRenderable(hidden);
  scene->addCamera(makeCameraWithTargetAndMask(target, 0x1u));

  const auto sceneResources =
      scene->getSceneLevelResources(Pass_Forward, target);
  EXPECT(countResourcesNamed(sceneResources, StringID("CameraUBO")) == 1,
         "camera resources remain target-driven even when one renderable is "
         "hidden");

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto &items = fg.getPasses()[0].queue.getItems();
  EXPECT(items.size() == 1, "hidden renderable stays filtered");
  if (items.size() == 1) {
    EXPECT(items[0].descriptorResources.size() == sceneResources.size(),
           "visible item still receives full scene-level resources");
  }
}

void testUnconfiguredIblResourcesAreNotInjected() {
  auto regular = makeRenderable("regular_no_ibl");
  auto ibl = makeIblRenderable();
  auto scene = Scene::create("ibl_injection");
  scene->addRenderable(regular);
  scene->addRenderable(ibl);
  scene->addCamera(makeCameraWithTargetAndMask(RenderTarget{}, Layer_All));

  RenderWorkQueue queue;
  queue.build(LX_core::RenderWorkBuildContext::realtime(*scene), Pass_Forward,
              RenderTarget{});

  bool sawRegular = false;
  bool sawIbl = false;
  for (const auto &item : queue.getItems()) {
    const bool itemConsumesIbl = std::any_of(
        item.shaderInfo->getReflectionBindings().begin(),
        item.shaderInfo->getReflectionBindings().end(),
        [](const auto &binding) { return binding.name == "IrradianceMap"; });
    bool hasIrradiance = false;
    bool hasPrefilter = false;
    bool hasBrdf = false;
    bool hasEnvironment = false;
    for (const auto &resource : item.descriptorResources) {
      const auto binding = resource.getBindingName();
      hasIrradiance = hasIrradiance || binding == StringID("IrradianceMap");
      hasPrefilter = hasPrefilter || binding == StringID("PrefilteredEnvMap");
      hasBrdf = hasBrdf || binding == StringID("BrdfLut");
      hasEnvironment = hasEnvironment || binding == StringID("EnvironmentUBO");
    }

    if (!itemConsumesIbl) {
      sawRegular = true;
      EXPECT(!hasIrradiance && !hasPrefilter && !hasBrdf && !hasEnvironment,
             "regular shader should not receive IBL resources");
    }
    if (itemConsumesIbl) {
      sawIbl = true;
      EXPECT(!hasIrradiance && !hasPrefilter && !hasBrdf && !hasEnvironment,
             "IBL shader should not receive environment resources when scene "
             "does not configure IBL");
    }
  }

  EXPECT(sawRegular, "regular item should be present");
  EXPECT(sawIbl, "IBL item should be present");
}

void testRenderUploadPlanCollectsRasterResourcesWithoutPushConstants() {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  auto vertexBuffer = VertexBuffer<VertexPos>::createUnique(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto indexBuffer = IndexBuffer::createUnique({0u, 1u, 2u});
  item.raster.vertexBuffer = GpuResourceRef{*vertexBuffer};
  item.raster.indexBuffer = GpuResourceRef{*indexBuffer};

  static const ShaderResourceBinding materialBinding{
      .name = "UploadPlanParams",
      .set = 0,
      .binding = 0,
      .type = ShaderPropertyType::UniformBuffer,
      .size = 16,
      .stageFlags = ShaderStage::Fragment,
  };
  auto uploadPlanParams =
      std::make_shared<ParameterBuffer>(StringID("UploadPlanParams"),
                                        materialBinding);
  item.descriptorResources.emplace_back(*uploadPlanParams);
  item.descriptorResources.emplace_back(*uploadPlanParams);

  RenderWorkQueue queue;
  queue.addItem(std::move(item));

  const RenderUploadPlan plan = buildRenderUploadPlan(queue);
  EXPECT(plan.resources.size() == 3, "upload plan should include unique "
                                     "vertex, index, and descriptor resources");
}

void testPartialIblResourcesAreNotCompletedWithDefaults() {
  auto ibl = makeIblRenderable();
  auto scene = Scene::create("partial_ibl_injection");
  scene->addRenderable(ibl);
  scene->addCamera(makeCameraWithTargetAndMask(RenderTarget{}, Layer_All));

  IblEnvironmentResources partial;
  partial.environmentUbo = std::make_unique<EnvironmentData>(1.0f, 5.0f);
  scene->resources().setIblEnvironmentResources(std::move(partial));

  RenderWorkQueue queue;
  queue.build(LX_core::RenderWorkBuildContext::realtime(*scene), Pass_Forward,
              RenderTarget{});
  EXPECT(queue.getItems().size() == 1,
         "partial IBL scene should still render the IBL item");
  if (queue.getItems().empty()) {
    return;
  }

  bool hasIrradiance = false;
  bool hasPrefilter = false;
  bool hasBrdf = false;
  bool hasEnvironment = false;
  for (const auto &resource : queue.getItems().front().descriptorResources) {
    const auto binding = resource.getBindingName();
    hasIrradiance = hasIrradiance || binding == StringID("IrradianceMap");
    hasPrefilter = hasPrefilter || binding == StringID("PrefilteredEnvMap");
    hasBrdf = hasBrdf || binding == StringID("BrdfLut");
    hasEnvironment = hasEnvironment || binding == StringID("EnvironmentUBO");
  }
  EXPECT(!hasIrradiance && !hasPrefilter && !hasBrdf && hasEnvironment,
         "partial IBL config should inject only explicitly configured "
         "environment resources");
}

void testRenderWorkQueueDebugCameraResourceUsesSceneResourceTableAndLayerMask() {
  auto visible = makeRenderableWithMask(Layer_Default, "debug_visible");
  auto overlay = makeRenderableWithMask(Layer_EditorOverlay, "debug_overlay");
  auto scene = Scene::create("debug_override");
  scene->addRenderable(visible);
  scene->addRenderable(overlay);

  auto cameraNode = makeCameraWithTarget(RenderTarget{});
  const auto camera = cameraNode->getComponent<CameraComponent>();
  scene->addCamera(cameraNode);
  const auto cameraResource =
      Scene::makeCameraResource(camera->get().getSnapshot());
  const auto expectedCameraResources =
      scene->getSceneLevelResources(Pass_Forward, cameraResource);

  RenderWorkQueue queue;
  queue.build(
      RenderWorkBuildContext::realtime(
          *scene,
          RenderWorkBuildContext::RealtimeOptions{
              .cameraResource = cameraResource,
              .visibleMask = Layer_All & ~Layer_EditorOverlay,
          }),
      Pass_Forward,
      RenderTarget{RenderTargetDesc::offscreenColor(ImageFormat::BGRA8)});

  EXPECT(queue.getItems().size() == 1,
         "debug render target should render only layers allowed by override");
  EXPECT(!expectedCameraResources.empty(),
         "explicit camera resource should produce scene-level descriptors");
  const bool hasExplicitCameraResource =
      !queue.getItems().empty() &&
      std::any_of(queue.getItems().front().descriptorResources.begin(),
                  queue.getItems().front().descriptorResources.end(),
                  [](const DescriptorResourceRef &resource) {
                    return resource.isResource() &&
                           resource.resource().isValid() &&
                           resource.getBindingName() == StringID("CameraUBO");
                  });
  EXPECT(!queue.getItems().empty() && hasExplicitCameraResource,
         "debug render target should use table-built camera resources");
}

void testDebugOnlyRenderableIsOverlayOnly() {
  auto regular = makeRenderableWithMask(Layer_Default, "regular_renderable");
  auto debugOnly =
      makeRenderableWithMask(Layer_Default, "debug_only_renderable");
  debugOnly->setDebugOnlyRenderable(true);

  auto scene = Scene::create("debug_only_filter");
  scene->addRenderable(regular);
  scene->addRenderable(debugOnly);
  scene->addCamera(makeCameraWithTargetAndMask(RenderTarget{}, Layer_All));

  RenderWorkQueue forwardQueue;
  forwardQueue.build(LX_core::RenderWorkBuildContext::realtime(*scene),
                     Pass_Forward, RenderTarget{});
  EXPECT(forwardQueue.getItems().size() == 1,
         "debug-only renderables must be excluded from normal passes");

  RenderWorkQueue overlayQueue;
  overlayQueue.build(LX_core::RenderWorkBuildContext::realtime(*scene),
                     Pass_DebugOverlay, RenderTarget{});
  EXPECT(overlayQueue.getItems().empty(),
         "debug-only flag should not force unsupported overlay materials into "
         "DebugOverlay");
}

void testSceneCreateDoesNotSeedHiddenLight() {
  auto scene = Scene::create("no_implicit_light");
  EXPECT(scene->getLights().empty(),
         "core Scene should not create hidden non-node lights");
}

void testInactiveCameraIsIgnoredForResourcesAndMasks() {
  const RenderTarget target{ImageFormat::BGRA8, ImageFormat::D32Float, 7};

  auto visible = makeRenderableWithMask(0x1u, "active_visible");
  auto hidden = makeRenderableWithMask(0x2u, "inactive_hidden");
  auto scene = Scene::create(visible);
  scene->addRenderable(hidden);

  auto activeCamera = makeCameraWithTargetAndMask(target, 0x1u);
  auto inactiveCamera = makeCameraWithTargetAndMask(target, 0x2u);
  inactiveCamera->getComponent<CameraComponent>()->get().setActive(false);
  scene->addCamera(activeCamera);
  scene->addCamera(inactiveCamera);

  const auto resources = scene->getSceneLevelResources(Pass_Forward, target);
  EXPECT(countResourcesNamed(resources, StringID("CameraUBO")) == 1,
         "inactive camera should be excluded while active camera resource "
         "remains");

  EXPECT(scene->getCombinedCameraCullingMask(target) == 0x1u,
         "inactive camera mask should not contribute to combined culling mask");

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  const auto &items = fg.getPasses()[0].queue.getItems();
  EXPECT(items.size() == 1,
         "inactive camera should not widen renderable visibility filtering");
}

void testEditorProjectedShadowPassKeepsCharacterCaster() {
  auto caster = makeRenderable("shadow_character", {}, true);
  auto scene = Scene::create(caster);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  auto light = makeLightWithPasses({Pass_Forward, Pass_Shadow});
  attachLightNode(scene, light, "shadow_light");

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, RenderTarget{}, {}});
  fg.addPass(FramePass{Pass_Shadow, RenderTarget{}, {}});
  fg.build(LX_core::RenderWorkBuildContext::realtime(*scene));

  EXPECT(fg.getPasses()[0].name == Pass_Forward,
         "Forward pass renders before projected shadows");
  EXPECT(fg.getPasses()[1].name == Pass_Shadow,
         "Shadow pass renders as overlay after Forward");
  EXPECT(fg.getPasses()[1].queue.getItems().size() == 1,
         "character caster appears in Shadow queue");
}

} // namespace

int main() {
  expSetEnvVK();

  testSingleRenderableSinglePass();
  testDuplicateRenderablesDedupe();
  testDifferentVariantKeepsTwo();
  testMeshOverlayMaterialPassUsesLineListGeometry();
  testFramePassNameIsStringID();
  testFrameGraphCompileAcceptsColorWriteThenSampleRead();
  testFrameGraphCompilePreservesSampledReadBindingName();
  testFrameGraphCompileAcceptsPostProcessSceneColorFlow();
  testFrameGraphCompileAcceptsBloomPostProcessChain();
  testFrameGraphCompileSortsPassesBySourceTargetDag();
  testFrameGraphCompileRejectsSelfProducedRead();
  testFrameGraphCompileAllowsReadModifyWriteWithEarlierProducer();
  testFrameGraphCompileRejectsImportedWriteTarget();
  testFrameGraphCompileRejectsInvalidWriteMode();
  testFrameGraphCompileRejectsMixedDuplicateWriteMode();
  testFrameGraphCompileRejectsSamePassDuplicateTarget();
  testFrameGraphCompileOrdersLegalDuplicateWritersByStableOrder();
  testFrameGraphCompileReaderWaitsForAllLegalDuplicateWriters();
  testFrameGraphCompileRejectsKnownSourceWithoutProducer();
  testFrameGraphCompileReportsUnknownSource();
  testFrameGraphCompileReportsUnknownTarget();
  testFrameGraphCompileReportsResourceCycle();
  testFrameGraphCompileReportsReversePhaseDependencyCycle();
  testFrameGraphCompileOrdersIndependentPassesByPhase();
  testFrameGraphCompileOrdersSamePhaseByStableOrder();
  testFrameGraphCompileOrdersSamePhaseAndStableOrderByOriginalIndex();
  testFrameGraphBuildPlanConsumesRenderPathGraph();
  testFrameGraphBuildPlanPreservesRenderPathGraphMetadata();
  testFrameGraphBuildPlanRejectsIncompleteRenderPathPass();
  testDefaultForwardRenderPathGraphPassSetValidation();
  testDefaultRenderPathGraphOrderComesFromSourceTargetDag();
  testDeferredLightingReadsComeFromRenderPathGraphSources();
  testDirectionalLightCascadeSplitsUpdateFromCamera();
  testDirectionalLightPendingDirectionUpdatesUboBeforeAttach();
  testDirectionalShadowDebugViewRecreatesCascadeMatrix();
  testDirectionalShadowCascadeStoresLightDepthRange();
  testDirectionalShadowCascadeUboSnapshotIsStable();
  testFrameGraphCompilePreservesTargetDescriptions();
  testRenderTargetToDescUsesMutatedLegacyFields();
  testRenderTargetDescPreservesMultipleColorFormats();
  testFrameGraphCompileReportsMissingRead();
  testFrameGraphCompileReportsDuplicateWrite();
  testFrameGraphCompileReportsUnnamedWrite();
  testBuildFromSceneIsIdempotent();
  testCollectAcrossMultiplePasses();
  testFrameGraphKeepsDifferentTargetsAsDifferentBuildDescs();
  testFrameGraphDedupesExactSameTargetBuildDescs();
  testPassFilterExcludesNonMatching();
  testForwardQueueDrawsOpaqueBeforeTransparentAndSortsTransparentBackToFront();
  testShadowQueueUsesFallbackVisibilityWhenNoShadowCamera();
  testMultiPassRebuildIsIdempotent();
  testMultiCameraTargetFilter();
  testOffscreenDepthTargetDoesNotMatchDefaultCamera();
  testMultiLightPassFilter();
  testNullOptCameraBeforeAndAfterFill();
  testVisibilityMaskFiltersRenderables();
  testVisibilityMaskOrsMatchingCameraMasks();
  testVisibilityFilteringKeepsSceneResources();
  testUnconfiguredIblResourcesAreNotInjected();
  testRenderUploadPlanCollectsRasterResourcesWithoutPushConstants();
  testPartialIblResourcesAreNotCompletedWithDefaults();
  testRenderWorkQueueDebugCameraResourceUsesSceneResourceTableAndLayerMask();
  testDebugOnlyRenderableIsOverlayOnly();
  testSceneCreateDoesNotSeedHiddenLight();
  testInactiveCameraIsIgnoredForResourcesAndMasks();
  testEditorProjectedShadowPassKeepsCharacterCaster();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all frame_graph tests passed\n";
  return 0;
}
