#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_target.hpp"
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
#include <iostream>
#include <memory>
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

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

class FakeShader : public IShader {
public:
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
               bool enableShadow = false) {
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
  entry.renderState = RenderState{};
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

std::shared_ptr<SceneNode>
makeRenderableWithMask(VisibilityLayerMask mask,
                       const std::string &shaderName = "fake_fg") {
  auto node = makeRenderable(shaderName);
  node->setVisibilityLayerMask(mask);
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testSingleRenderableSinglePass() {
  auto r = makeRenderable();
  auto scene = makeSceneWithDefaultCamera(r);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

  auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 2,
         "different variants keep two distinct PipelineBuildDesc");
}

void testFramePassNameIsStringID() {
  FramePass p{Pass_Forward, {}, {}};
  EXPECT(p.name == Pass_Forward, "FramePass.name is a StringID compared by id");
}

void testFrameGraphCompileAcceptsColorWriteThenSampleRead() {
  const auto offscreen =
      FrameGraphResourceRef::colorAttachment(StringID("test.color"));
  const auto swapColor =
      FrameGraphResourceRef::colorAttachment(StringID("swapchain.color"));
  const auto swapDepth =
      FrameGraphResourceRef::depthAttachment(StringID("swapchain.depth"));

  FrameGraph graph;
  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                          {},
                          {},
                          {FrameGraphWrite{offscreen}}});
  graph.addPass(FramePass{
      Pass_DebugOverlay,
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float),
      {},
      {FrameGraphRead::sampled(offscreen.name)},
      {FrameGraphWrite{swapColor}, FrameGraphWrite{swapDepth}}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(), "compile should accept write then sampled read");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
}

void testFrameGraphCompilePreservesSampledReadBindingName() {
  const auto shadowDepth =
      FrameGraphResourceRef::depthAttachment(StringID("shadow.cascade0"));

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

void testFrameGraphCompilePreservesFullscreenProceduralPass() {
  auto material = MaterialInstance::create(MaterialTemplate::create("fullscreen"));
  FramePass pass{
      Pass_DebugOverlay,
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
      {},
      {FrameGraphRead::sampled(StringID("scene.color"), StringID("SceneColor"))},
      {FrameGraphWrite{
          FrameGraphResourceRef::colorAttachment(StringID("procedural.color"))}}};
  pass.kind = FramePassKind::FullscreenProcedural;
  pass.fullscreenMaterial = material;

  FrameGraph graph;
  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                          {},
                          {},
                          {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                              StringID("scene.color"))}}});
  graph.addPass(std::move(pass));

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(),
         "compile should accept fullscreen procedural pass after scene color");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
  if (compiled.getPasses().size() == 2) {
    EXPECT(compiled.getPasses()[1].kind ==
               FramePassKind::FullscreenProcedural,
           "fullscreen pass kind should be preserved");
    EXPECT(compiled.getPasses()[1].reads[0].bindingName ==
               StringID("SceneColor"),
           "fullscreen pass sampled binding should be preserved");
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
      debugCamera->get().getProjMatrix() * debugCamera->get().getViewMatrix();
  EXPECT(approxMatrix(debugViewProj,
                      light.getDirectionalUBO()->param.cascadeViewProj[0]),
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
      light.getDirectionalUBO()->param.cascadeDepthRanges.x;
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
             light.getDirectionalUBO()->getBackendCacheIdentity(),
         "snapshot should not overwrite the main directional light UBO");
  EXPECT(cascade0->getBackendCacheIdentity() !=
             cascade3->getBackendCacheIdentity(),
         "each cascade snapshot needs an independent GPU buffer");
  EXPECT(approxMatrix(cascade0->param.shadowViewProj,
                      light.getDirectionalUBO()->param.cascadeViewProj[0]),
         "cascade 0 snapshot should use cascade 0 matrix");
  EXPECT(approxMatrix(cascade3->param.shadowViewProj,
                      light.getDirectionalUBO()->param.cascadeViewProj[3]),
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
      FrameGraphResourceRef::colorAttachment(StringID("shared.color"));

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
  EXPECT(errors.find("shared.color") != std::string::npos,
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

  fg.buildFromScene(*scene);
  fg.buildFromScene(
      *scene); // second call should clear + refill, not accumulate

  EXPECT(fg.getPasses()[0].queue.getItems().size() == 1,
         "buildFromScene clears previous items on re-entry");
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
  fg.buildFromScene(*scene);

  const auto &passes = fg.getPasses();
  EXPECT(passes.size() == 2, "two passes configured");
  EXPECT(passes[0].queue.getItems().size() == 2,
         "Forward pass: both renderables match");
  EXPECT(passes[1].queue.getItems().size() == 1,
         "Shadow pass: only rA supports shadow");
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
  fg.buildFromScene(*scene);

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
  EXPECT(resA.size() == 1, "Forward targetA: camA UBO only");

  // For targetB: only camB.
  auto resB = scene->getSceneLevelResources(Pass_Forward, targetB);
  EXPECT(resB.size() == 1, "Forward targetB: camB UBO only");

  // Cross-check camera UBO identity: camA's UBO should be in resA but not resB.
  if (resA.size() == 1 && resB.size() == 1) {
    const auto camAUbo = std::dynamic_pointer_cast<IGpuResource>(
        camA->getComponent<CameraComponent>()->get().getUBO());
    EXPECT(resA[0] == camAUbo, "resA[0] is camA's UBO");
    EXPECT(resB[0] != camAUbo, "resB[0] is NOT camA's UBO");
  }
}

void testOffscreenDepthTargetDoesNotMatchDefaultCamera() {
  auto scene = makeSceneWithDefaultCamera(makeRenderable());

  const auto depthOnly =
      RenderTarget{RenderTargetDesc::offscreenDepth(ImageFormat::D32Float)};
  const auto resources = scene->getSceneLevelResources(Pass_Forward, depthOnly);

  EXPECT(resources.empty(),
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
  EXPECT(resForward.size() == 4,
         "Pass_Forward: 1 cam + 2 matching LightUBOs + SceneLightsUBO");

  auto resShadow = scene->getSceneLevelResources(Pass_Shadow, RenderTarget{});
  EXPECT(resShadow.size() == 4,
         "Pass_Shadow: 1 cam + 2 matching LightUBOs + SceneLightsUBO");
}

void testNullOptCameraBeforeAndAfterFill() {
  // REQ-009: a camera with nullopt target never matches. After setTarget it
  // matches exactly that target.
  const RenderTarget customTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 3};

  auto testCam = makeCameraNoTarget();
  auto scene = Scene::create(makeRenderable());
  scene->addCamera(testCam);

  auto resBefore = scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(resBefore.empty(),
         "nullopt camera excludes camera and Scene has no hidden light");

  testCam->getComponent<CameraComponent>()->get().setTarget(customTarget);

  auto resAfter = scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(resAfter.size() == 1, "after setTarget(customTarget): camera UBO");

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
  fg.buildFromScene(*scene);
  fg.buildFromScene(*scene); // second call must clear + refill, not accumulate.

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
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

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
  EXPECT(sceneResources.size() == 1, "camera resources remain target-driven "
                                     "even when one renderable is hidden");

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.buildFromScene(*scene);

  const auto &items = fg.getPasses()[0].queue.getItems();
  EXPECT(items.size() == 1, "hidden renderable stays filtered");
  if (items.size() == 1) {
    EXPECT(items[0].descriptorResources.size() == sceneResources.size(),
           "visible item still receives full scene-level resources");
  }
}

void testRenderQueueDebugOverrideUsesExplicitResourcesAndLayerMask() {
  auto visible = makeRenderableWithMask(Layer_Default, "debug_visible");
  auto overlay = makeRenderableWithMask(Layer_EditorOverlay, "debug_overlay");
  auto scene = Scene::create("debug_override");
  scene->addRenderable(visible);
  scene->addRenderable(overlay);

  auto cameraNode = makeCameraWithTarget(RenderTarget{});
  const auto camera = cameraNode->getComponent<CameraComponent>();
  scene->addCamera(cameraNode);

  RenderQueue queue;
  queue.buildFromSceneWithOverrides(
      *scene, Pass_Forward,
      RenderTarget{RenderTargetDesc::offscreenColor(ImageFormat::BGRA8)},
      {camera->get().getUBO()}, Layer_All & ~Layer_EditorOverlay);

  EXPECT(queue.getItems().size() == 1,
         "debug render target should render only layers allowed by override");
  EXPECT(!queue.getItems().empty() &&
             std::find(queue.getItems().front().descriptorResources.begin(),
                       queue.getItems().front().descriptorResources.end(),
                       camera->get().getUBO()) !=
                 queue.getItems().front().descriptorResources.end(),
         "debug render target should use explicit camera resources");
}

void testDebugOnlyRenderableIsOverlayOnly() {
  auto regular = makeRenderableWithMask(Layer_Default, "regular_renderable");
  auto debugOnly = makeRenderableWithMask(Layer_Default, "debug_only_renderable");
  debugOnly->setDebugOnlyRenderable(true);

  auto scene = Scene::create("debug_only_filter");
  scene->addRenderable(regular);
  scene->addRenderable(debugOnly);
  scene->addCamera(makeCameraWithTargetAndMask(RenderTarget{}, Layer_All));

  RenderQueue forwardQueue;
  forwardQueue.buildFromScene(*scene, Pass_Forward, RenderTarget{});
  EXPECT(forwardQueue.getItems().size() == 1,
         "debug-only renderables must be excluded from normal passes");

  RenderQueue overlayQueue;
  overlayQueue.buildFromScene(*scene, Pass_DebugOverlay, RenderTarget{});
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
  EXPECT(resources.size() == 1, "inactive camera should be excluded while "
                                "active camera resource remains");

  EXPECT(scene->getCombinedCameraCullingMask(target) == 0x1u,
         "inactive camera mask should not contribute to combined culling mask");

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.buildFromScene(*scene);

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
  fg.buildFromScene(*scene);

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
  testFramePassNameIsStringID();
  testFrameGraphCompileAcceptsColorWriteThenSampleRead();
  testFrameGraphCompilePreservesSampledReadBindingName();
  testFrameGraphCompilePreservesFullscreenProceduralPass();
  testDirectionalLightCascadeSplitsUpdateFromCamera();
  testDirectionalShadowDebugViewRecreatesCascadeMatrix();
  testDirectionalShadowCascadeStoresLightDepthRange();
  testDirectionalShadowCascadeUboSnapshotIsStable();
  testFrameGraphCompilePreservesTargetDescriptions();
  testRenderTargetToDescUsesMutatedLegacyFields();
  testFrameGraphCompileReportsMissingRead();
  testFrameGraphCompileReportsDuplicateWrite();
  testFrameGraphCompileReportsUnnamedWrite();
  testBuildFromSceneIsIdempotent();
  testCollectAcrossMultiplePasses();
  testFrameGraphKeepsDifferentTargetsAsDifferentBuildDescs();
  testFrameGraphDedupesExactSameTargetBuildDescs();
  testPassFilterExcludesNonMatching();
  testShadowQueueUsesFallbackVisibilityWhenNoShadowCamera();
  testMultiPassRebuildIsIdempotent();
  testMultiCameraTargetFilter();
  testOffscreenDepthTargetDoesNotMatchDefaultCamera();
  testMultiLightPassFilter();
  testNullOptCameraBeforeAndAfterFill();
  testVisibilityMaskFiltersRenderables();
  testVisibilityMaskOrsMatchingCameraMasks();
  testVisibilityFilteringKeepsSceneResources();
  testRenderQueueDebugOverrideUsesExplicitResourcesAndLayerMask();
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
