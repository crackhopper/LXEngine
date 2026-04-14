#include "core/gpu/render_resource.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/material.hpp"
#include "core/resources/mesh.hpp"
#include "core/resources/pipeline_build_info.hpp"
#include "core/resources/shader.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/scene/frame_graph.hpp"
#include "core/scene/object.hpp"
#include "core/scene/pass.hpp"
#include "core/scene/render_queue.hpp"
#include "core/scene/scene.hpp"
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

  const void *getRawData() const override { return nullptr; }
  u32 getByteSize() const override { return 0; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

class FakeMaterial : public IMaterial {
public:
  FakeMaterial(MaterialTemplate::Ptr tmpl,
               ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : m_template(std::move(tmpl)), m_passFlag(passFlag) {}
  std::vector<IRenderResourcePtr> getDescriptorResources() const override {
    return {};
  }
  IShaderPtr getShaderInfo() const override {
    return m_template ? m_template->getShader() : nullptr;
  }
  ResourcePassFlag getPassFlag() const override { return m_passFlag; }
  RenderState getRenderState() const override { return {}; }

  StringID getRenderSignature(StringID pass) const override {
    StringID passSig =
        m_template ? m_template->getRenderPassSignature(pass) : StringID{};
    StringID fields[] = {passSig};
    return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
  }

private:
  MaterialTemplate::Ptr m_template;
  ResourcePassFlag m_passFlag;
};

std::shared_ptr<RenderableSubMesh>
makeRenderable(const std::string &shaderName = "fake_fg",
               const std::vector<ShaderVariant> &variants = {},
               ResourcePassFlag passFlag = ResourcePassFlag::Forward) {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  auto mesh = Mesh::create(vb, ib);

  auto shader = std::make_shared<FakeShader>();
  auto tmpl = MaterialTemplate::create(shaderName, shader);

  ShaderProgramSet ps;
  ps.shaderName = shaderName;
  ps.variants = variants;
  RenderPassEntry entry;
  entry.shaderSet = ps;
  entry.renderState = RenderState{};
  tmpl->setPass(Pass_Forward, std::move(entry));
  // Shadow pass entries are intentionally omitted — the FakeMaterial's
  // pass mask (m_passFlag) is what drives supportsPass filtering in
  // RenderQueue::buildFromScene; pass-template population is orthogonal.

  auto material = std::make_shared<FakeMaterial>(tmpl, passFlag);
  return std::make_shared<RenderableSubMesh>(mesh, material, nullptr);
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

  auto infos = fg.collectAllPipelineBuildInfos();
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
  auto infos = fg.collectAllPipelineBuildInfos();
  EXPECT(infos.size() == 1,
         "duplicate configs collapse to one PipelineBuildInfo");
}

void testDifferentVariantKeepsTwo() {
  auto r1 = makeRenderable("fake_fg", {});
  auto r2 = makeRenderable("fake_fg", {ShaderVariant{"HAS_NORMAL_MAP", true}});
  auto scene = Scene::create(r1);
  scene->addRenderable(r2);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, {}, {}});
  fg.buildFromScene(*scene);

  auto infos = fg.collectAllPipelineBuildInfos();
  EXPECT(infos.size() == 2,
         "different variants keep two distinct PipelineBuildInfo");
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

void testPassMaskFilterExcludesNonMatching() {
  // Renderable A participates in Forward + Shadow; B only in Forward.
  auto rA = makeRenderable("fake_fg_a", {},
                           ResourcePassFlag::Forward | ResourcePassFlag::Shadow);
  auto rB = makeRenderable("fake_fg_b", {}, ResourcePassFlag::Forward);
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
         "Shadow pass: only rA (which has Shadow bit)");
}

void testPassFlagFromStringIDSmoke() {
  // Smoke test: the helper wired up in REQ-008 R1 returns the right flag.
  EXPECT(passFlagFromStringID(Pass_Forward) == ResourcePassFlag::Forward,
         "Pass_Forward → Forward");
  EXPECT(passFlagFromStringID(Pass_Shadow) == ResourcePassFlag::Shadow,
         "Pass_Shadow → Shadow");
  EXPECT(passFlagFromStringID(Pass_Deferred) == ResourcePassFlag::Deferred,
         "Pass_Deferred → Deferred");
  // Unknown IDs return zero flag.
  EXPECT(static_cast<u32>(passFlagFromStringID(
             GlobalStringTable::get().Intern("UnknownPass"))) == 0u,
         "unknown pass → 0");
}

void testMultiPassRebuildIsIdempotent() {
  auto rA = makeRenderable("fake_fg_a", {},
                           ResourcePassFlag::Forward | ResourcePassFlag::Shadow);
  auto rB = makeRenderable("fake_fg_b", {}, ResourcePassFlag::Forward);
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

  auto infos = fg.collectAllPipelineBuildInfos();
  EXPECT(infos.size() == 1,
         "same pipeline key across two passes dedupes to 1 info");
}

} // namespace

int main() {
  VertexFactory::registerType<VertexPos>();

  testSingleRenderableSinglePass();
  testDuplicateRenderablesDedupe();
  testDifferentVariantKeepsTwo();
  testFramePassNameIsStringID();
  testBuildFromSceneIsIdempotent();
  testCollectAcrossMultiplePasses();
  testPassFlagFromStringIDSmoke();
  testPassMaskFilterExcludesNonMatching();
  testMultiPassRebuildIsIdempotent();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all frame_graph tests passed\n";
  return 0;
}
