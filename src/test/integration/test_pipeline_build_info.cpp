#include "core/rhi/image_format.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
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
// Minimal fakes — same shape as test_pipeline_identity.cpp
// ---------------------------------------------------------------------------

class FakeShader : public IShader {
public:
  FakeShader(std::vector<ShaderResourceBinding> bindings,
             std::vector<ShaderStageCode> stages)
      : m_bindings(std::move(bindings)), m_stages(std::move(stages)) {}

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
  std::string getShaderName() const override { return "fake_shader"; }

private:
  std::vector<ShaderResourceBinding> m_bindings;
  std::vector<ShaderStageCode> m_stages;
};

RenderingItem
buildItem(PrimitiveTopology topo = PrimitiveTopology::TriangleList) {
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
      ShaderResourceBinding{"MaterialUBO",
                            1,
                            0,
                            ShaderPropertyType::UniformBuffer,
                            1,
                            32,
                            0,
                            ShaderStage::Fragment,
                            {}},
      ShaderResourceBinding{"albedoTex",
                            1,
                            1,
                            ShaderPropertyType::Texture2D,
                            1,
                            0,
                            0,
                            ShaderStage::Fragment,
                            {}},
  };
  std::vector<ShaderStageCode> stages = {
      ShaderStageCode{ShaderStage::Vertex,
                      std::vector<uint32_t>{0x07230203, 0, 0}},
      ShaderStageCode{ShaderStage::Fragment,
                      std::vector<uint32_t>{0x07230203, 1, 0}},
  };

  auto shader =
      std::make_shared<FakeShader>(std::move(bindings), std::move(stages));
  auto tmpl = MaterialTemplate::create("fake_shader");

  ShaderProgramSet set;
  set.shaderName = "fake_shader";
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  entry.renderState.cullMode = CullMode::Front;
  entry.renderState.depthTestEnable = false;
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  auto material = MaterialInstance::create(tmpl);

  // Minimal vertex + index buffers.
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2}, topo);
  auto mesh = Mesh::create(vb, ib);

  auto sub = std::make_shared<RenderableSubMesh>(mesh, material, nullptr);
  auto scene = Scene::create(sub);
  return LX_test::firstItemFromScene(*scene, Pass_Forward);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testFromRenderingItemPopulatesBindings() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderingItem(item);
  EXPECT(info.bindings.size() == 3, "bindings.size()==3");
  if (info.bindings.size() == 3) {
    EXPECT(info.bindings[0].name == "CameraUBO", "binding 0 name");
    EXPECT(info.bindings[1].name == "MaterialUBO", "binding 1 name");
    EXPECT(info.bindings[2].name == "albedoTex", "binding 2 name");
  }
}

void testFromRenderingItemKeyMatches() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderingItem(item);
  EXPECT(info.key == item.pipelineKey, "key matches item.pipelineKey");
}

void testFromRenderingItemStagesPreserved() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderingItem(item);
  EXPECT(info.stages.size() == 2, "stages.size()==2");
}

void testFromRenderingItemTopology() {
  auto item1 = buildItem(PrimitiveTopology::TriangleList);
  auto info1 = PipelineBuildDesc::fromRenderingItem(item1);
  EXPECT(info1.topology == PrimitiveTopology::TriangleList, "topology tri");

  auto item2 = buildItem(PrimitiveTopology::LineList);
  auto info2 = PipelineBuildDesc::fromRenderingItem(item2);
  EXPECT(info2.topology == PrimitiveTopology::LineList, "topology line");
}

void testFromRenderingItemRenderStateFromMaterial() {
  auto item = buildItem();
  auto info = PipelineBuildDesc::fromRenderingItem(item);
  // MaterialInstance resolves render state from the template's pass definition.
  EXPECT(info.renderState.cullMode == CullMode::Front,
         "renderState cull comes from material");
  EXPECT(info.renderState.depthTestEnable == false,
         "renderState depthTest comes from material");
}

void testFromRenderingItemIsDeterministic() {
  auto item = buildItem();
  auto a = PipelineBuildDesc::fromRenderingItem(item);
  auto b = PipelineBuildDesc::fromRenderingItem(item);
  EXPECT(a.key == b.key, "deterministic key");
  EXPECT(a.bindings.size() == b.bindings.size(), "deterministic bindings size");
  EXPECT(a.topology == b.topology, "deterministic topology");
}

void testRenderTargetHashStability() {
  RenderTarget a;
  RenderTarget b;
  EXPECT(a.getHash() == b.getHash(), "default RenderTarget hashes equal");

  RenderTarget c;
  c.sampleCount = 4;
  EXPECT(c.getHash() != a.getHash(), "different sampleCount → different hash");
}

} // namespace

int main() {
  expSetEnvVK();
  VertexFactory::registerType<VertexPos>();

  testFromRenderingItemPopulatesBindings();
  testFromRenderingItemKeyMatches();
  testFromRenderingItemStagesPreserved();
  testFromRenderingItemTopology();
  testFromRenderingItemRenderStateFromMaterial();
  testFromRenderingItemIsDeterministic();
  testRenderTargetHashStability();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all pipeline_build_info tests passed\n";
  return 0;
}
