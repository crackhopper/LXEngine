#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/camera.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"

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
  findBinding(uint32_t, uint32_t) const override {
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &) const override {
    return std::nullopt;
  }
  size_t getProgramHash() const override { return 0; }
  std::string getShaderName() const override { return "fake_fg"; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

std::shared_ptr<RenderableSubMesh>
makeRenderable(const std::string &shaderName = "fake_fg",
               const std::vector<ShaderVariant> &variants = {},
               bool enableShadow = false) {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  auto mesh = Mesh::create(vb, ib);

  auto shader = std::make_shared<FakeShader>();
  auto tmpl = MaterialTemplate::create(shaderName);

  ShaderProgramSet ps;
  ps.shaderName = shaderName;
  ps.variants = variants;
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
  return std::make_shared<RenderableSubMesh>(mesh, material, nullptr);
}

// Helpers for REQ-009 scenarios.
CameraPtr makeCameraWithTarget(const RenderTarget &target) {
  auto cam = std::make_shared<Camera>();
  cam->setTarget(target);
  return cam;
}

CameraPtr makeCameraNoTarget() {
  return std::make_shared<Camera>();
}

std::shared_ptr<DirectionalLight>
makeLightWithPasses(std::initializer_list<StringID> passes) {
  auto light = std::make_shared<DirectionalLight>();
  light->setSupportedPasses(passes);
  return light;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testSingleRenderableSinglePass() {
  auto r = makeRenderable();
  auto scene = Scene::create(r);

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
  auto scene = Scene::create(r1);
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
  auto scene = Scene::create(r1);
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
  auto scene = Scene::create(r);
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
  auto scene = Scene::create(rA);
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
  // Scene::Scene auto-adds a default camera at RenderTarget{} (sampleCount=1)
  // and a default DirectionalLight with Forward/Deferred support. We pick
  // targetA/B with distinct sampleCount so the auto camera does NOT match.
  const RenderTarget targetA{ImageFormat::BGRA8, ImageFormat::D32Float, 2};
  const RenderTarget targetB{ImageFormat::BGRA8, ImageFormat::D32Float, 4};

  auto camA = makeCameraWithTarget(targetA);
  auto camB = makeCameraWithTarget(targetB);

  auto scene = Scene::create(makeRenderable());
  scene->addCamera(camA);
  scene->addCamera(camB);

  // For targetA: camA matches, camB doesn't, default camera doesn't (sc=1≠2).
  // Default DirectionalLight matches Pass_Forward.
  auto resA = scene->getSceneLevelResources(Pass_Forward, targetA);
  EXPECT(resA.size() == 2,
         "Forward×targetA: camA UBO + default light UBO (2 entries)");

  // For targetB: only camB + default light.
  auto resB = scene->getSceneLevelResources(Pass_Forward, targetB);
  EXPECT(resB.size() == 2,
         "Forward×targetB: camB UBO + default light UBO (2 entries)");

  // Cross-check camera UBO identity: camA's UBO should be in resA but not resB.
  if (resA.size() == 2 && resB.size() == 2) {
    const auto camAUbo =
        std::dynamic_pointer_cast<IGpuResource>(camA->getUBO());
    EXPECT(resA[0] == camAUbo, "resA[0] is camA's UBO");
    EXPECT(resB[0] != camAUbo, "resB[0] is NOT camA's UBO");
  }
}

void testMultiLightPassFilter() {
  // REQ-009: light filtered by supportsPass. Scene::Scene auto-adds one
  // DirectionalLight (Forward + Deferred) which counts toward both
  // Forward and Deferred but not Shadow.
  auto lightForward = makeLightWithPasses({Pass_Forward});
  auto lightShadow = makeLightWithPasses({Pass_Shadow});
  auto lightBoth = makeLightWithPasses({Pass_Forward, Pass_Shadow});

  auto scene = Scene::create(makeRenderable());
  scene->addLight(lightForward);
  scene->addLight(lightShadow);
  scene->addLight(lightBoth);

  // Pass_Forward: default camera (sc=1) matches default target, default light
  // matches Forward, lightForward matches, lightShadow does NOT, lightBoth
  // matches. Total = 1 cam + 3 lights = 4.
  auto resForward = scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
  EXPECT(resForward.size() == 4,
         "Pass_Forward: 1 cam + default light + lightForward + lightBoth = 4");

  // Pass_Shadow: default camera matches, default light does NOT support
  // Shadow, lightShadow matches, lightBoth matches, lightForward does NOT.
  // Total = 1 cam + 2 lights = 3.
  auto resShadow = scene->getSceneLevelResources(Pass_Shadow, RenderTarget{});
  EXPECT(resShadow.size() == 3,
         "Pass_Shadow: 1 cam + lightShadow + lightBoth = 3");
}

void testNullOptCameraBeforeAndAfterFill() {
  // REQ-009: a camera with nullopt target never matches. After setTarget it
  // matches exactly that target. Use a non-default target (sc=3) so we're
  // isolated from the auto-added camera at sc=1.
  const RenderTarget customTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 3};

  auto testCam = makeCameraNoTarget(); // m_target == nullopt
  auto scene = Scene::create(makeRenderable());
  scene->addCamera(testCam);

  // Before setTarget: no camera matches customTarget (auto cam is sc=1,
  // testCam is nullopt). Default DirectionalLight still matches Pass_Forward
  // regardless of target. So result should be 0 cameras + 1 light = 1.
  auto resBefore =
      scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(resBefore.size() == 1,
         "nullopt camera does not match customTarget (1 default-light only)");

  // Fill the target on testCam.
  testCam->setTarget(customTarget);

  // After setTarget: testCam matches customTarget → 1 camera + 1 light = 2.
  auto resAfter =
      scene->getSceneLevelResources(Pass_Forward, customTarget);
  EXPECT(resAfter.size() == 2,
         "after setTarget(customTarget): testCam UBO + default light = 2");

  // And sanity check: matchesTarget agrees.
  EXPECT(testCam->matchesTarget(customTarget),
         "testCam->matchesTarget(customTarget) after setTarget");
}

void testMultiPassRebuildIsIdempotent() {
  auto rA = makeRenderable("fake_fg_a", {}, true);
  auto rB = makeRenderable("fake_fg_b");
  auto scene = Scene::create(rA);
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
  auto scene = Scene::create(r);

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

} // namespace

int main() {
  expSetEnvVK();
  VertexFactory::registerType<VertexPos>();

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

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all frame_graph tests passed\n";
  return 0;
}
