#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/image_format.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"

#include "scene_test_helpers.hpp"

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

void keepResourceAlive(std::shared_ptr<IGpuResource> resource) {
  static std::vector<std::shared_ptr<IGpuResource>> resources;
  resources.push_back(std::move(resource));
}

RenderWorkItem
buildItem(PrimitiveTopology topo = PrimitiveTopology::TriangleList,
          std::vector<VertexInputAttribute> vertexInputs = {},
          const RenderTarget &target = {}) {
  std::vector<ShaderResourceBinding> bindings = {
      ShaderResourceBinding{"FullscreenParams",
                            0,
                            0,
                            ShaderPropertyType::UniformBuffer,
                            1,
                            16,
                            0,
                            ShaderStage::Fragment,
                            {}},
  };
  std::vector<ShaderStageCode> stages = {
      ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203, 0, 0}},
      ShaderStageCode{ShaderStage::Fragment,
                      std::vector<u32>{0x07230203, 1, 0}},
  };

  auto shader = std::make_shared<FakeShader>(
      std::move(bindings), std::move(stages), std::move(vertexInputs));

  ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = "direct_helper_shader";
  shaderProgram.shader = shader;

  // Minimal vertex + index buffers.
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2}, topo);
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::DirectRasterPass;
  item.directRaster.purpose = DirectRasterPassPurpose::TestOnlyNonMaterial;
  item.shaderInfo = shader;
  item.shaderProgram = shaderProgram;
  item.renderState = RenderState{};
  item.renderState.cullMode = CullMode::Front;
  item.renderState.depthTestEnable = false;
  item.directRaster.vertexBuffer = GpuResourceRef{*vb};
  item.directRaster.indexBuffer = GpuResourceRef{*ib};
  item.directRaster.indexCount = static_cast<u32>(ib->indexCount());
  item.directRaster.instanceCount = 1;
  item.pass = Pass_PostProcess;
  item.target = target.toDesc();
  item.objectSignature = StringID("pipeline_build_info.direct_helper_object");
  item.materialSignature = StringID("pipeline_build_info.direct_helper_state");
  item.materialTypeVariant = shaderProgram.getPipelineSignature();
  item.renderPathNodeSignature =
      LX_test::testRenderPathNodeSignature(Pass_PostProcess, target);
  item.pipelineKey =
      PipelineKey::build(item.materialTypeVariant,
                         item.renderPathNodeSignature);

  keepResourceAlive(vb);
  keepResourceAlive(ib);
  return item;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testFromRenderWorkItemPopulatesBindings() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(info.bindings.size() == 1, "bindings.size()==1");
  if (info.bindings.size() == 1) {
    EXPECT(info.bindings[0].name == "FullscreenParams", "binding 0 name");
  }
}

void testFromRenderWorkItemKeyMatches() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(info.key == item.pipelineKey, "key matches item.pipelineKey");
}

void testFromRenderWorkItemCarriesHelperShaderProgramSignature() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(!item.shaderProgram.hasEnabledVariant("LX_MATERIAL_CONTRACT_SOURCE"),
         "direct helper fixture must not carry material source shader variant");
  EXPECT(info.shaderVariantKey == item.shaderProgram.getPipelineSignature(),
         "build desc shader variant key should match render work shader "
         "program signature");
}

void testFromRenderWorkItemRejectsMaterialSourceDirectRasterPass() {
  auto item = buildItem();
  item.shaderProgram.variants.push_back(ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .materialContractSource = ResourceUri(
          "assets://shaders/glsl/common/materials/matte.contract.glsl"),
      .materialSourceSignature = StringID("matte-source-signature"),
  });
  bool rejected = false;
  try {
    (void)PipelineBuildDesc::fromRenderWorkItem(item);
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("material-source geometry") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "material-source DirectRasterPass must not produce pipeline desc");
}

void testFromRenderWorkItemRejectsUnclassifiedDirectRasterPass() {
  auto item = buildItem();
  item.directRaster.purpose = DirectRasterPassPurpose::Unspecified;
  bool rejected = false;
  try {
    (void)PipelineBuildDesc::fromRenderWorkItem(item);
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("non-material helper purpose") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "DirectRasterPass must declare an explicit non-material helper "
         "purpose");
}

void testFromRenderWorkItemRejectsMaterialSourceSceneBindings() {
  auto item = buildItem();
  std::vector<ShaderResourceBinding> bindings = {
      ShaderResourceBinding{"SceneMaterials",
                            0,
                            7,
                            ShaderPropertyType::StorageBuffer,
                            1,
                            96,
                            0,
                            ShaderStage::Fragment,
                            {}},
  };
  std::vector<ShaderStageCode> stages = {
      ShaderStageCode{ShaderStage::Fragment,
                      std::vector<u32>{0x07230203, 1, 0}},
  };
  auto shader = std::make_shared<FakeShader>(std::move(bindings),
                                             std::move(stages));
  item.shaderInfo = shader;
  item.shaderProgram.shader = shader;

  bool rejected = false;
  try {
    (void)PipelineBuildDesc::fromRenderWorkItem(item);
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("material-source geometry") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "SceneMaterials DirectRasterPass must not produce pipeline desc");
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

void testFromRenderWorkItemRenderStateFromHelperItem() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderWorkItem(item);
  EXPECT(info.renderState.cullMode == CullMode::Front,
         "renderState cull comes from helper item");
  EXPECT(info.renderState.depthTestEnable == false,
         "renderState depthTest comes from helper item");
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

void testFromRenderWorkItemPreservesRenderPathRenderingContract() {
  auto item = buildItem();
  item.renderingMode = RenderPathNodeRenderingMode::Dynamic;
  item.attachments = {
      RenderPathAttachmentContract{
          .target = "scene.hdrColor",
          .format = ImageFormat::RGBA16Float,
          .samples = 1,
          .layers = 1,
          .depth = false,
      },
      RenderPathAttachmentContract{
          .target = "scene.depth",
          .format = ImageFormat::D32Float,
          .samples = 1,
          .layers = 1,
          .depth = true,
      },
  };

  auto info = PipelineBuildDesc::fromRenderWorkItem(item);

  EXPECT(info.renderingMode == RenderPathNodeRenderingMode::Dynamic,
         "build desc should preserve render path rendering mode");
  EXPECT(info.attachments.size() == 2,
         "build desc should preserve attachment contracts");
  if (info.attachments.size() == 2) {
    EXPECT(info.attachments[0].target == "scene.hdrColor",
           "color attachment target preserved");
    EXPECT(info.attachments[0].format == ImageFormat::RGBA16Float,
           "color attachment format preserved");
    EXPECT(!info.attachments[0].depth, "color attachment depth flag");
    EXPECT(info.attachments[1].target == "scene.depth",
           "depth attachment target preserved");
    EXPECT(info.attachments[1].format == ImageFormat::D32Float,
           "depth attachment format preserved");
    EXPECT(info.attachments[1].depth, "depth attachment depth flag");
  }
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
  ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = "filtered_layout_helper_shader";
  shaderProgram.shader = shader;

  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::DirectRasterPass;
  item.directRaster.purpose = DirectRasterPassPurpose::TestOnlyNonMaterial;
  item.shaderInfo = shader;
  item.shaderProgram = shaderProgram;
  item.renderState = RenderState{};
  item.directRaster.vertexBuffer = GpuResourceRef{*vb};
  item.directRaster.indexBuffer = GpuResourceRef{*ib};
  item.directRaster.indexCount = static_cast<u32>(ib->indexCount());
  item.directRaster.instanceCount = 1;
  item.pass = Pass_PostProcess;
  item.target = RenderTarget{}.toDesc();
  item.objectSignature = StringID("pipeline_build_info.filtered_helper_object");
  item.materialSignature = StringID("pipeline_build_info.filtered_helper_state");
  item.materialTypeVariant = shaderProgram.getPipelineSignature();
  item.renderPathNodeSignature =
      LX_test::testRenderPathNodeSignature(Pass_PostProcess, RenderTarget{});
  item.pipelineKey =
      PipelineKey::build(item.materialTypeVariant,
                         item.renderPathNodeSignature);
  keepResourceAlive(vb);
  keepResourceAlive(ib);

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

} // namespace

int main() {
  expSetEnvVK();

  testFromRenderWorkItemPopulatesBindings();
  testFromRenderWorkItemKeyMatches();
  testFromRenderWorkItemCarriesHelperShaderProgramSignature();
  testFromRenderWorkItemRejectsMaterialSourceDirectRasterPass();
  testFromRenderWorkItemRejectsUnclassifiedDirectRasterPass();
  testFromRenderWorkItemRejectsMaterialSourceSceneBindings();
  testFromRenderWorkItemStagesPreserved();
  testFromRenderWorkItemTopology();
  testFromRenderWorkItemRenderStateFromHelperItem();
  testFromRenderWorkItemIsDeterministic();
  testFromRenderWorkItemPreservesTargetDesc();
  testFromRenderWorkItemPreservesRenderPathRenderingContract();
  testFromRenderWorkItemFiltersVertexLayoutToShaderInputs();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all pipeline_build_info tests passed\n";
  return 0;
}
