#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/scene/light.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "scene_test_helpers.hpp"

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
  auto node = SceneNode::create("fg_camera_target_" +
                                std::to_string(++cameraCounter));
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

SceneSharedPtr makeSceneWithDefaultCamera(const SceneNodeSharedPtr &root = nullptr) {
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
  EXPECT(resA.size() == 3,
         "Forward×targetA: camA UBO + default light UBO + SceneLightsUBO");

  // For targetB: only camB + default light + aggregated scene lights.
  auto resB = scene->getSceneLevelResources(Pass_Forward, targetB);
  EXPECT(resB.size() == 3,
         "Forward×targetB: camB UBO + default light UBO + SceneLightsUBO");

  // Cross-check camera UBO identity: camA's UBO should be in resA but not resB.
  if (resA.size() == 3 && resB.size() == 3) {
    const auto camAUbo = std::dynamic_pointer_cast<IGpuResource>(
        camA->getComponent<CameraComponent>()->get().getUBO());
    EXPECT(resA[0] == camAUbo, "resA[0] is camA's UBO");
    EXPECT(resB[0] != camAUbo, "resB[0] is NOT camA's UBO");
  }
}

void testMultiLightPassFilter() {
  // REQ-009: light filtered by supportsPass.
  auto lightForward = makeLightWithPasses({Pass_Forward});
  auto lightShadow = makeLightWithPasses({Pass_Shadow});
  auto lightBoth = makeLightWithPasses({Pass_Forward, Pass_Shadow});

  auto scene = Scene::create(makeRenderable());
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  scene->addLight(lightForward);
  scene->addLight(lightShadow);
  scene->addLight(lightBoth);

  auto resForward = scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
  EXPECT(resForward.size() == 5,
         "Pass_Forward: 1 cam + 3 directional LightUBOs + SceneLightsUBO");

  auto resShadow = scene->getSceneLevelResources(Pass_Shadow, RenderTarget{});
  EXPECT(resShadow.size() == 4,
         "Pass_Shadow: 1 cam + 2 directional LightUBOs + SceneLightsUBO");
}

void testNullOptCameraBeforeAndAfterFill() {
  // REQ-009: a camera with nullopt target never matches. After setTarget it
  // matches exactly that target.
  const RenderTarget customTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 3};

  auto testCam = makeCameraNoTarget();
  auto scene = Scene::create(makeRenderable());
  scene->addCamera(testCam);

  auto resBefore =
      scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(resBefore.size() == 2,
         "nullopt camera excludes camera, leaving default LightUBO + SceneLightsUBO");

  testCam->getComponent<CameraComponent>()->get().setTarget(customTarget);

  auto resAfter =
      scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(resAfter.size() == 3,
         "after setTarget(customTarget): camera + default LightUBO + SceneLightsUBO");

  EXPECT(testCam->getComponent<CameraComponent>()->get().matchesTarget(customTarget),
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
    EXPECT(items[0].pipelineKey ==
               visible->getValidatedPassData(Pass_Forward)->get().pipelineKey,
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

  const auto sceneResources = scene->getSceneLevelResources(Pass_Forward, target);
  EXPECT(sceneResources.size() == 3,
         "camera and light resources remain target-driven even when one renderable is hidden");

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
  EXPECT(resources.size() == 3,
         "inactive camera should be excluded while active camera and light resources remain");

  EXPECT(scene->getCombinedCameraCullingMask(target) == 0x1u,
         "inactive camera mask should not contribute to combined culling mask");

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, target, {}});
  fg.buildFromScene(*scene);

  const auto &items = fg.getPasses()[0].queue.getItems();
  EXPECT(items.size() == 1,
         "inactive camera should not widen renderable visibility filtering");
}

} // namespace

int main() {
  expSetEnvVK();

  testSingleRenderableSinglePass();
  testDuplicateRenderablesDedupe();
  testDifferentVariantKeepsTwo();
  testFramePassNameIsStringID();
  testBuildFromSceneIsIdempotent();
  testCollectAcrossMultiplePasses();
  testPassFilterExcludesNonMatching();
  testMultiPassRebuildIsIdempotent();
  testMultiCameraTargetFilter();
  testMultiLightPassFilter();
  testNullOptCameraBeforeAndAfterFill();
  testVisibilityMaskFiltersRenderables();
  testVisibilityMaskOrsMatchingCameraMasks();
  testVisibilityFilteringKeepsSceneResources();
  testInactiveCameraIsIgnoredForResourcesAndMasks();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all frame_graph tests passed\n";
  return 0;
}
