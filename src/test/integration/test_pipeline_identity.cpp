#include "core/rhi/index_buffer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/skeleton.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"

#include <iostream>
#include <memory>
#include <optional>
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
// Minimal fakes: avoid the Vulkan / shaderc path so this test has no GPU deps.
// ---------------------------------------------------------------------------

class FakeShader : public IShader {
public:
  FakeShader() = default;
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

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

// Reusable vertex + index + shader builders.
struct Fixture {
  VertexBufferSharedPtr vb;
  std::shared_ptr<IndexBuffer> ib;
  MeshSharedPtr mesh;
  MaterialTemplate::SharedPtr tmpl;
  MaterialInstanceSharedPtr material;

  static Fixture
  make(const std::string &shaderName = "pbr",
       const std::vector<ShaderVariant> &variants = {},
       const RenderState &state = {},
       PrimitiveTopology topo = PrimitiveTopology::TriangleList) {
    Fixture f;
    f.vb = VertexBuffer<VertexPos>::create(
        std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
    f.ib = IndexBuffer::create({0, 1, 2}, topo);
    f.mesh = Mesh::create(f.vb, f.ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});

    auto shader = std::make_shared<FakeShader>();
    f.tmpl = MaterialTemplate::create(shaderName);

    ShaderProgramSet ps;
    ps.shaderName = shaderName;
    ps.variants = variants;
    ps.shader = shader;

    MaterialPassDefinition entry;
    entry.shaderProgram = ps;
    entry.renderState = state;
    f.tmpl->setPassDefinition(Pass_Forward, std::move(entry));
    f.material = MaterialInstance::create(f.tmpl);
    return f;
  }
};

PipelineKey buildKey(const Fixture &f, StringID pass,
                     const SkeletonSharedPtr &skel = nullptr,
                     const RenderTargetDesc &target =
                         RenderTargetDesc::swapchain(ImageFormat::BGRA8,
                                                     ImageFormat::D32Float),
                     std::optional<RenderPathGeometryContract> geometry =
                         RenderPathGeometryContract{}) {
  (void)skel;
  const auto shaderProgram = f.material->getPassShaderProgram(pass);
  EXPECT(shaderProgram.has_value(), "fixture material must expose pass shader");

  FramePass renderPathPass;
  renderPathPass.name = pass;
  renderPathPass.target = target;
  renderPathPass.shaderUri = ResourceUri("test://pipeline_identity/" +
                                         shaderProgram->get().shaderName);
  renderPathPass.renderState = f.material->getPassRenderState(pass);
  renderPathPass.renderingMode = RenderPathNodeRenderingMode::Dynamic;
  renderPathPass.input.geometry = geometry;

  const StringID materialTypeVariant =
      f.material->getMaterialTypeVariantSignature(shaderProgram->get());
  const StringID renderPathNodeSignature =
      getFramePassRenderPathNodeSignature(renderPathPass);
  return PipelineKey::build(materialTypeVariant, renderPathNodeSignature);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testEqualConfigsProduceSameKey() {
  auto f1 = Fixture::make();
  auto f2 = Fixture::make();
  PipelineKey k1 = buildKey(f1, Pass_Forward);
  PipelineKey k2 = buildKey(f2, Pass_Forward);
  EXPECT(k1 == k2, "identical configs must yield identical pipeline keys");
}

void testVariantChangeProducesDifferentKey() {
  auto f1 = Fixture::make("pbr", {});
  auto f2 =
      Fixture::make("pbr", {ShaderVariant{"HAS_NORMAL_MAP", true}});
  PipelineKey k1 = buildKey(f1, Pass_Forward);
  PipelineKey k2 = buildKey(f2, Pass_Forward);
  EXPECT(k1 != k2, "enabling a variant must change the pipeline key");
}

void testObjectTopologyChangeDoesNotProduceDifferentKey() {
  auto f1 = Fixture::make("pbr", {}, RenderState{},
                          PrimitiveTopology::TriangleList);
  auto f2 = Fixture::make("pbr", {}, RenderState{},
                          PrimitiveTopology::LineList);
  PipelineKey k1 = buildKey(f1, Pass_Forward);
  PipelineKey k2 = buildKey(f2, Pass_Forward);
  EXPECT(k1 == k2,
         "object topology alone must not be a PipelineKey axis");
}

void testRenderPathTopologyContractChangeProducesDifferentKey() {
  auto f = Fixture::make("pbr", {}, RenderState{},
                         PrimitiveTopology::TriangleList);
  RenderPathGeometryContract triangle;
  triangle.topology = PrimitiveTopology::TriangleList;
  RenderPathGeometryContract line;
  line.topology = PrimitiveTopology::LineList;
  PipelineKey kTriangle = buildKey(f, Pass_Forward, nullptr,
                                   RenderTargetDesc::swapchain(
                                       ImageFormat::BGRA8,
                                       ImageFormat::D32Float),
                                   triangle);
  PipelineKey kLine = buildKey(f, Pass_Forward, nullptr,
                               RenderTargetDesc::swapchain(
                                   ImageFormat::BGRA8,
                                   ImageFormat::D32Float),
                               line);
  EXPECT(kTriangle != kLine,
         "RenderPathNode topology contract must change the pipeline key");
}

void testRenderPathGeometryMismatchRejectsPreparedDesc() {
  auto f = Fixture::make("pbr", {}, RenderState{},
                         PrimitiveTopology::TriangleList);
  auto node = SceneNode::create("geometry_mismatch_node");
  node->addComponent<MeshComponent>(f.mesh);
  node->addComponent<MaterialComponent>(f.material);

  auto scene = Scene::create("geometry_mismatch_scene");
  scene->addRenderable(node);
  auto cameraNode = SceneNode::create("geometry_mismatch_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach");
  camera->get().setTarget(RenderTarget{});
  camera->get().updateMatrices();
  scene->addCamera(cameraNode);

  RenderPathGeometryContract lineGeometry;
  lineGeometry.topology = PrimitiveTopology::LineList;

  FramePass renderPathPass;
  renderPathPass.name = Pass_Forward;
  renderPathPass.target = RenderTargetDesc::swapchain(ImageFormat::BGRA8,
                                                      ImageFormat::D32Float);
  renderPathPass.stage = RenderPassStage::Raster;
  renderPathPass.dispatch = RenderPassDispatch::Draw;
  renderPathPass.input.kind = RenderPassInputKind::SceneRenderables;
  renderPathPass.input.geometry = lineGeometry;

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context = RenderWorkBuildContext::realtime(*scene);
  compiler.buildInputs(renderPathPass, context, inputs);
  const std::vector<RenderInputDesc> descs =
      compiler.prepare(renderPathPass, context, inputs);
  const bool rejectedForGeometry = std::any_of(
      descs.begin(), descs.end(), [](const RenderInputDesc &desc) {
        return std::any_of(
            desc.diagnostics.begin(), desc.diagnostics.end(),
            [](const RenderInputDiagnostic &diagnostic) {
              return diagnostic.code ==
                     RenderInputDiagnosticCode::GeometryContractMismatch;
            });
      });
  EXPECT(rejectedForGeometry,
         "prepared desc must reject object topology incompatible with "
         "RenderPathNode geometry");
}

void testSkeletonPresenceDoesNotProduceDifferentKey() {
  auto f = Fixture::make();
  auto skel = Skeleton::create({});
  PipelineKey noSkel = buildKey(f, Pass_Forward, nullptr);
  PipelineKey withSkel = buildKey(f, Pass_Forward, skel);
  EXPECT(noSkel == withSkel,
         "adding a skeleton alone must not change the pipeline key");
}

void testDifferentPassProducesDifferentKey() {
  // Build a single template with two distinct pass entries (Forward vs Shadow),
  // differing in render state (cull mode) so each entry signature is unique.
  auto f = Fixture::make();

  ShaderProgramSet ps;
  ps.shaderName = "pbr";

  RenderState shadowState;
  shadowState.cullMode = CullMode::Front; // flip to make signature differ

  MaterialPassDefinition shadowEntry;
  shadowEntry.shaderProgram = ps;
  shadowEntry.renderState = shadowState;
  f.tmpl->setPassDefinition(Pass_Shadow, std::move(shadowEntry));

  auto node = SceneNode::create("pipeline_identity_pass_node");
  node->addComponent<MeshComponent>(f.mesh);
  node->addComponent<MaterialComponent>(f.material);
  PipelineKey kFwd = buildKey(f, Pass_Forward);
  PipelineKey kSh = buildKey(f, Pass_Shadow);

  EXPECT(kFwd != kSh, "different passes yield different pipeline keys");
}

void testToDebugStringSmoke() {
  auto f =
      Fixture::make("pbr", {ShaderVariant{"HAS_NORMAL_MAP", true}});
  PipelineKey k = buildKey(f, Pass_Forward);
  std::string s = GlobalStringTable::get().toDebugString(k.id);
  EXPECT(s.rfind("PipelineKey(", 0) == 0,
         "debug string must start with 'PipelineKey(', got: " + s);
  EXPECT(s.find("MaterialTypeVariant(") != std::string::npos,
         "debug string must contain MaterialTypeVariant(, got: " + s);
  EXPECT(s.find("RenderPathNode(") != std::string::npos,
         "debug string must contain RenderPathNode(, got: " + s);
  EXPECT(s.find("ObjectRender(") == std::string::npos,
         "debug string must not contain ObjectRender(, got: " + s);
  EXPECT(s.find("TargetRender(") == std::string::npos,
         "debug string must not contain TargetRender(, got: " + s);
  std::cout << "  debug: " << s << "\n";
}

void testPipelineKeyIncludesMaterialSourceSignature() {
  auto f1 = Fixture::make();
  auto f2 = Fixture::make();
  f1.material->setMaterialSourceSignature(StringID("source-a"));
  f2.material->setMaterialSourceSignature(StringID("source-b"));

  const PipelineKey k1 = buildKey(f1, Pass_Forward);
  const PipelineKey k2 = buildKey(f2, Pass_Forward);

  EXPECT(k1 != k2,
         "different material source signatures must change pipeline key");

  const std::string debug = GlobalStringTable::get().toDebugString(k1.id);
  EXPECT(debug.find("MaterialTypeVariant(") != std::string::npos,
         "debug string must contain MaterialTypeVariant(, got: " + debug);
  EXPECT(debug.find("source-a") != std::string::npos,
         "debug string must include material source signature, got: " + debug);
}

void testPipelineKeyIncludesTargetSignature() {
  auto f = Fixture::make();

  const auto swapchain =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  const auto offscreen = RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);
  const auto depthOnly = RenderTargetDesc::offscreenDepth(ImageFormat::D32Float);

  const PipelineKey kSwap = buildKey(f, Pass_Forward, nullptr, swapchain);
  const PipelineKey kOff = buildKey(f, Pass_Forward, nullptr, offscreen);
  const PipelineKey kDepth = buildKey(f, Pass_Forward, nullptr, depthOnly);

  EXPECT(kSwap != kOff, "swapchain and offscreen targets must not collide");
  EXPECT(kSwap != kDepth, "swapchain and depth-only targets must not collide");
  EXPECT(kOff != kDepth, "offscreen color and depth-only targets must not collide");

  const std::string debug = GlobalStringTable::get().toDebugString(kSwap.id);
  EXPECT(debug.find("TargetRender(") == std::string::npos,
         "debug string must not contain TargetRender(, got: " + debug);
  EXPECT(debug.find("RenderPathNode(") != std::string::npos,
         "debug string must contain RenderPathNode(, got: " + debug);
  EXPECT(debug.find("RenderTarget:role=swapchain") != std::string::npos,
         "RenderPathNode debug string must include render target contract, got: " +
             debug);
}

} // namespace

int main() {
  expSetEnvVK();

  testEqualConfigsProduceSameKey();
  testVariantChangeProducesDifferentKey();
  testObjectTopologyChangeDoesNotProduceDifferentKey();
  testRenderPathTopologyContractChangeProducesDifferentKey();
  testRenderPathGeometryMismatchRejectsPreparedDesc();
  testSkeletonPresenceDoesNotProduceDifferentKey();
  testDifferentPassProducesDifferentKey();
  testToDebugStringSmoke();
  testPipelineKeyIncludesMaterialSourceSignature();
  testPipelineKeyIncludesTargetSignature();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all pipeline identity tests passed\n";
  return 0;
}
