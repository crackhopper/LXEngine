#include "core/rhi/index_buffer.hpp"
#include "core/asset/material.hpp"
#include "core/asset/mesh.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/skeleton.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
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
  findBinding(uint32_t, uint32_t) const override {
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &) const override {
    return std::nullopt;
  }
  size_t getProgramHash() const override { return 0; }

  const void *getRawData() const override { return nullptr; }
  u32 getByteSize() const override { return 0; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

class FakeMaterial : public IMaterial {
public:
  FakeMaterial(MaterialTemplate::Ptr tmpl) : m_template(std::move(tmpl)) {}

  std::vector<IRenderResourcePtr> getDescriptorResources() const override {
    return {};
  }
  IShaderPtr getShaderInfo() const override {
    return m_template ? m_template->getShader() : nullptr;
  }
  ResourcePassFlag getPassFlag() const override {
    return ResourcePassFlag::Forward;
  }
  RenderState getRenderState(StringID pass) const override {
    (void)pass;
    return {};
  }

  StringID getRenderSignature(StringID pass) const override {
    StringID passSig =
        m_template ? m_template->getRenderPassSignature(pass) : StringID{};
    StringID fields[] = {passSig};
    return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
  }

private:
  MaterialTemplate::Ptr m_template;
};

// Reusable vertex + index + shader builders.
struct Fixture {
  VertexBufferPtr vb;
  std::shared_ptr<IndexBuffer> ib;
  MeshPtr mesh;
  MaterialTemplate::Ptr tmpl;
  std::shared_ptr<FakeMaterial> material;

  static Fixture
  make(const std::string &shaderName = "blinnphong_0",
       const std::vector<ShaderVariant> &variants = {},
       const RenderState &state = {},
       PrimitiveTopology topo = PrimitiveTopology::TriangleList) {
    Fixture f;
    f.vb = VertexFactory::create(
        std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
    f.ib = IndexBuffer::create({0, 1, 2}, topo);
    f.mesh = Mesh::create(f.vb, f.ib);

    auto shader = std::make_shared<FakeShader>();
    f.tmpl = MaterialTemplate::create(shaderName, shader);

    ShaderProgramSet ps;
    ps.shaderName = shaderName;
    ps.variants = variants;

    RenderPassEntry entry;
    entry.shaderSet = ps;
    entry.renderState = state;
    f.tmpl->setPass(Pass_Forward, std::move(entry));
    f.material = std::make_shared<FakeMaterial>(f.tmpl);
    return f;
  }
};

PipelineKey buildKey(const Fixture &f, StringID pass,
                     const SkeletonPtr &skel = nullptr) {
  auto sub = std::make_shared<RenderableSubMesh>(f.mesh, f.material, skel);
  StringID objSig = sub->getRenderSignature(pass);
  StringID matSig = f.material->getRenderSignature(pass);
  return PipelineKey::build(objSig, matSig);
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
  auto f1 = Fixture::make("blinnphong_0", {});
  auto f2 =
      Fixture::make("blinnphong_0", {ShaderVariant{"HAS_NORMAL_MAP", true}});
  PipelineKey k1 = buildKey(f1, Pass_Forward);
  PipelineKey k2 = buildKey(f2, Pass_Forward);
  EXPECT(k1 != k2, "enabling a variant must change the pipeline key");
}

void testTopologyChangeProducesDifferentKey() {
  auto f1 = Fixture::make("blinnphong_0", {}, RenderState{},
                          PrimitiveTopology::TriangleList);
  auto f2 = Fixture::make("blinnphong_0", {}, RenderState{},
                          PrimitiveTopology::LineList);
  PipelineKey k1 = buildKey(f1, Pass_Forward);
  PipelineKey k2 = buildKey(f2, Pass_Forward);
  EXPECT(k1 != k2, "topology change must change the pipeline key");
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
  ps.shaderName = "blinnphong_0";

  RenderState shadowState;
  shadowState.cullMode = CullMode::Front; // flip to make signature differ

  RenderPassEntry shadowEntry;
  shadowEntry.shaderSet = ps;
  shadowEntry.renderState = shadowState;
  f.tmpl->setPass(Pass_Shadow, std::move(shadowEntry));

  auto sub = std::make_shared<RenderableSubMesh>(f.mesh, f.material, nullptr);
  StringID objSig = sub->getRenderSignature(Pass_Forward);
  StringID fwdMat = f.material->getRenderSignature(Pass_Forward);
  StringID shMat = f.material->getRenderSignature(Pass_Shadow);
  PipelineKey kFwd = PipelineKey::build(objSig, fwdMat);
  PipelineKey kSh = PipelineKey::build(objSig, shMat);

  EXPECT(kFwd != kSh, "different passes yield different pipeline keys");
}

void testToDebugStringSmoke() {
  auto f =
      Fixture::make("blinnphong_0", {ShaderVariant{"HAS_NORMAL_MAP", true}});
  PipelineKey k = buildKey(f, Pass_Forward);
  std::string s = GlobalStringTable::get().toDebugString(k.id);
  EXPECT(s.rfind("PipelineKey(", 0) == 0,
         "debug string must start with 'PipelineKey(', got: " + s);
  EXPECT(s.find("ObjectRender(") != std::string::npos,
         "debug string must contain ObjectRender(, got: " + s);
  EXPECT(s.find("MaterialRender(") != std::string::npos,
         "debug string must contain MaterialRender(, got: " + s);
  std::cout << "  debug: " << s << "\n";
}

} // namespace

int main() {
  // Register the vertex type with the factory (same pattern used elsewhere).
  VertexFactory::registerType<VertexPos>();

  testEqualConfigsProduceSameKey();
  testVariantChangeProducesDifferentKey();
  testTopologyChangeProducesDifferentKey();
  testSkeletonPresenceDoesNotProduceDifferentKey();
  testDifferentPassProducesDifferentKey();
  testToDebugStringSmoke();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all pipeline identity tests passed\n";
  return 0;
}
