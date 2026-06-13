#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/image_format.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
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
// Minimal fakes — same shape as test_pipeline_identity.cpp
// ---------------------------------------------------------------------------

class FakeShader : public IShader {
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
  usize getProgramHash() const override { return 0; }
  std::string getShaderName() const override { return "fake_shader"; }

private:
  std::vector<ShaderResourceBinding> m_bindings;
  std::vector<ShaderStageCode> m_stages;
  std::vector<VertexInputAttribute> m_vertexInputs;
};

SceneNodeSharedPtr makeCameraNodeWithTarget(const RenderTarget &target) {
  static int cameraCounter = 0;
  auto node = SceneNode::create("pipeline_build_info_camera_" +
                                std::to_string(++cameraCounter));
  auto camera = node->addComponent<CameraComponent>();
  camera->get().setTarget(target);
  camera->get().updateMatrices();
  return node;
}

void configureMaterialV2UploadData(const MaterialInstanceSharedPtr &material) {
  material->setBsdfType("matte");
  material->setMaterialSourceUri(ResourceUri(
      "assets://shaders/glsl/common/materials/matte.contract.glsl"));
  material->setMaterialSourceReflectionHash("matte-source-contract-v1");
  material->setMaterialSourceSignature(StringID("matte-source-signature"));
  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{0.25f, 0.5f, 0.75f};
  material->setMaterialEnvelope(StringID("Kd"), std::move(kd));
  material->syncGpuData();
}

RenderWorkItem
buildItem(PrimitiveTopology topo = PrimitiveTopology::TriangleList,
          std::vector<VertexInputAttribute> vertexInputs = {},
          const RenderTarget &target = {}) {
  std::vector<ShaderResourceBinding> bindings = {
      ShaderResourceBinding{"CameraUBO",
                            0,
                            0,
                            ShaderPropertyType::UniformBuffer,
                            1,
                            192,
                            0,
                            ShaderStage::Vertex,
                            {}},
  };
  if (topo == PrimitiveTopology::TriangleList) {
    bindings.push_back(ShaderResourceBinding{"SceneMaterials",
                                             0,
                                             7,
                                             ShaderPropertyType::StorageBuffer,
                                             1,
                                             96,
                                             0,
                                             ShaderStage::Fragment,
                                             {}});
    bindings.push_back(ShaderResourceBinding{"SceneTextures",
                                             0,
                                             11,
                                             ShaderPropertyType::Texture2D,
                                             256,
                                             0,
                                             0,
                                             ShaderStage::Fragment,
                                             {}});
  }
  std::vector<ShaderStageCode> stages = {
      ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203, 0, 0}},
      ShaderStageCode{ShaderStage::Fragment,
                      std::vector<u32>{0x07230203, 1, 0}},
  };

  auto shader = std::make_shared<FakeShader>(
      std::move(bindings), std::move(stages), std::move(vertexInputs));
  auto tmpl = MaterialTemplate::create("fake_shader");

  ShaderProgramSet set;
  set.shaderName = "fake_shader";
  set.variants.push_back(ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .materialContractSource = ResourceUri(
          "assets://shaders/glsl/common/materials/matte.contract.glsl"),
      .materialSourceSignature = StringID("matte-source-signature"),
  });
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  entry.renderState.cullMode = CullMode::Front;
  entry.renderState.depthTestEnable = false;
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();
  auto material = MaterialInstance::create(tmpl);
  configureMaterialV2UploadData(material);

  // Minimal vertex + index buffers.
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2}, topo);
  auto mesh = Mesh::create(vb, ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});

  auto node = SceneNode::create("pipeline_build_info_node");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);
  auto scene = Scene::create(node);
  scene->addCamera(makeCameraNodeWithTarget(target));
  auto item = LX_test::firstItemFromScene(*scene, Pass_Forward, target);
  static std::vector<SceneSharedPtr> keepAliveScenes;
  keepAliveScenes.push_back(std::move(scene));
  return item;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testFromRenderWorkItemPopulatesBindings() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(info.bindings.size() == 3, "bindings.size()==3");
  if (info.bindings.size() == 3) {
    EXPECT(info.bindings[0].name == "CameraUBO", "binding 0 name");
    EXPECT(info.bindings[1].name == "SceneMaterials", "binding 1 name");
    EXPECT(info.bindings[2].name == "SceneTextures", "binding 2 name");
  }
}

void testFromRenderWorkItemKeyMatches() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(info.key == item.pipelineKey, "key matches item.pipelineKey");
}

void testFromRenderWorkItemCarriesMaterialSourceVariant() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(item.shaderProgram.hasEnabledVariant("LX_MATERIAL_CONTRACT_SOURCE"),
         "render work should carry material source shader variant");
  EXPECT(info.shaderVariantKey == item.shaderProgram.getPipelineSignature(),
         "build desc shader variant key should match render work shader "
         "program signature");
}

void testFromRenderWorkItemStagesPreserved() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(info.stages.size() == 2, "stages.size()==2");
}

void testFromRenderWorkItemTopology() {
  auto item1 = buildItem(PrimitiveTopology::TriangleList);
  auto info1 = PipelineBuildDesc::fromRenderWorkItem(item1);
  EXPECT(info1.topology == PrimitiveTopology::TriangleList, "topology tri");

  auto item2 = buildItem(PrimitiveTopology::LineList);
  auto info2 = PipelineBuildDesc::fromRenderWorkItem(item2);
  EXPECT(info2.topology == PrimitiveTopology::LineList, "topology line");
}

void testFromRenderWorkItemRenderStateFromMaterial() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  // MaterialInstance resolves render state from the template's pass definition.
  EXPECT(info.renderState.cullMode == CullMode::Front,
         "renderState cull comes from material");
  EXPECT(info.renderState.depthTestEnable == false,
         "renderState depthTest comes from material");
}

void testFromRenderWorkItemIsDeterministic() {
  auto item = buildItem();
  auto a = PipelineBuildDesc::fromRenderWorkItem(item);
  auto b = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(a.key == b.key, "deterministic key");
  EXPECT(a.bindings.size() == b.bindings.size(), "deterministic bindings size");
  EXPECT(a.topology == b.topology, "deterministic topology");
}

void testFromRenderWorkItemPreservesTargetDesc() {
  const auto targetDesc =
      RenderTargetDesc::offscreenDepth(ImageFormat::D32Float);
  const RenderTarget target{targetDesc};
  auto item = buildItem(PrimitiveTopology::TriangleList, {}, target);
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);

  EXPECT(item.target == targetDesc, "rendering item should carry target desc");
  EXPECT(info.target == targetDesc, "build desc should preserve target desc");
}

void testFromRenderWorkItemFiltersVertexLayoutToShaderInputs() {
  using V = VertexPosNormalUvBone;
  auto vb = VertexBuffer<V>::create({
      V({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      V({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      V({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
  });
  auto ib = IndexBuffer::create({0u, 1u, 2u});
  auto mesh = Mesh::create(vb, ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});

  std::vector<ShaderResourceBinding> bindings = {
      ShaderResourceBinding{"CameraUBO",
                            0,
                            0,
                            ShaderPropertyType::UniformBuffer,
                            1,
                            192,
                            0,
                            ShaderStage::Vertex,
                            {}},
  };
  std::vector<ShaderStageCode> stages = {
      ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203, 0, 0}},
  };
  std::vector<VertexInputAttribute> shaderInputs = {
      {"inPos", 0, DataType::Float3},
      {"inNormal", 1, DataType::Float3},
  };

  auto shader = std::make_shared<FakeShader>(
      std::move(bindings), std::move(stages), std::move(shaderInputs));
  auto tmpl = MaterialTemplate::create("filtered_layout_shader");
  ShaderProgramSet set;
  set.shaderName = "filtered_layout_shader";
  set.variants.push_back(ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .materialContractSource = ResourceUri(
          "assets://shaders/glsl/common/materials/matte.contract.glsl"),
      .materialSourceSignature = StringID("matte-source-signature"),
  });
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();

  auto material = MaterialInstance::create(tmpl);
  configureMaterialV2UploadData(material);
  auto node = SceneNode::create("filtered_layout_node");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);
  auto scene = Scene::create(node);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());

  auto item = LX_test::firstItemFromScene(*scene, Pass_Forward);
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  const auto &items = info.vertexLayout.getItems();
  EXPECT(items.size() == 2, "vertex layout should drop unused attributes");
  if (items.size() == 2) {
    EXPECT(items[0].location == 0, "filtered location 0 preserved");
    EXPECT(items[1].location == 1, "filtered location 1 preserved");
  }
  EXPECT(info.vertexLayout.getStride() == sizeof(V),
         "filtered layout keeps original stride");
}

ShaderProgramSet makeSourceVariantProgramSet(const ResourceUri &sourceUri,
                                             StringID sourceSignature) {
  ShaderProgramSet set;
  set.shaderName = "fake_shader";
  set.variants.push_back(ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .materialContractSource = sourceUri,
      .materialSourceSignature = sourceSignature,
  });
  return set;
}

void testMaterialSourceVariantAffectsShaderProgramSignature() {
  const ResourceUri matteSource(
      "assets://shaders/glsl/common/materials/matte.contract.glsl");
  const ResourceUri metalSource(
      "assets://shaders/glsl/common/materials/metal.contract.glsl");

  const ShaderProgramSet matte =
      makeSourceVariantProgramSet(matteSource, StringID("matte-source-sig"));
  const ShaderProgramSet texturedMatte =
      makeSourceVariantProgramSet(matteSource, StringID("matte-source-sig"));
  const ShaderProgramSet metal =
      makeSourceVariantProgramSet(metalSource, StringID("metal-source-sig"));

  EXPECT(matte.getPipelineSignature() == texturedMatte.getPipelineSignature(),
         "texture presence must not produce shader variant key");
  EXPECT(matte.getPipelineSignature() != metal.getPipelineSignature(),
         "different bsdf.source should produce different shader variant key");
}

} // namespace

int main() {
  expSetEnvVK();

  testFromRenderWorkItemPopulatesBindings();
  testFromRenderWorkItemKeyMatches();
  testFromRenderWorkItemCarriesMaterialSourceVariant();
  testFromRenderWorkItemStagesPreserved();
  testFromRenderWorkItemTopology();
  testFromRenderWorkItemRenderStateFromMaterial();
  testFromRenderWorkItemIsDeterministic();
  testFromRenderWorkItemPreservesTargetDesc();
  testFromRenderWorkItemFiltersVertexLayoutToShaderInputs();
  testMaterialSourceVariantAffectsShaderProgramSignature();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all pipeline_build_info tests passed\n";
  return 0;
}
