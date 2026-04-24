#include "core/asset/material_instance.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace LX_core;
using namespace LX_infra;

namespace {

int s_failures = 0;

#define REQUIRE(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  FAIL: " #cond "  (" << __FILE__ << ":" << __LINE__       \
                << ")\n";                                                      \
      ++s_failures;                                                            \
      return;                                                                  \
    }                                                                          \
  } while (0)

class FakeShader : public IShader {
public:
  explicit FakeShader(std::vector<ShaderResourceBinding> bindings)
      : m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }
  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &item : m_bindings) {
      if (item.set == set && item.binding == binding)
        return std::cref(item);
    }
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &item : m_bindings) {
      if (item.name == name)
        return std::cref(item);
    }
    return std::nullopt;
  }
  usize getProgramHash() const override { return 0; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

std::filesystem::path findShaderDir() {
  std::filesystem::path cwd = std::filesystem::current_path();
  for (int i = 0; i < 4; ++i) {
    auto candidate = cwd / "shaders" / "glsl";
    if (std::filesystem::exists(candidate / "blinnphong_0.vert") &&
        std::filesystem::exists(candidate / "blinnphong_0.frag"))
      return candidate;
    auto parent = cwd.parent_path();
    if (parent == cwd)
      break;
    cwd = parent;
  }
  return {};
}

MaterialInstanceSharedPtr buildInstanceFromBlinnPhong() {
  auto dir = findShaderDir();
  if (dir.empty()) {
    std::cerr << "  SETUP: blinnphong_0 shaders not found; skipping test\n";
    return nullptr;
  }
  auto compile = ShaderCompiler::compileProgram(dir / "blinnphong_0.vert",
                                                dir / "blinnphong_0.frag", {});
  if (!compile.success) {
    std::cerr << "  SETUP: compile failed: " << compile.errorMessage << "\n";
    return nullptr;
  }
  auto bindings = ShaderReflector::reflect(compile.stages);
  auto vertexInputs = ShaderReflector::reflectVertexInputs(compile.stages);
  auto shader = std::make_shared<CompiledShader>(std::move(compile.stages),
                                                 bindings, vertexInputs,
                                                 "blinnphong_0");

  auto tmpl = MaterialTemplate::create("blinnphong_0");
  ShaderProgramSet set;
  set.shaderName = "blinnphong_0";
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();

  return MaterialInstance::create(tmpl);
}

MaterialTemplate::SharedPtr buildMultiPassTemplate(const RenderState &forwardState,
                                             const RenderState &shadowState) {
  ShaderResourceBinding binding;
  binding.name = "MaterialUBO";
  binding.set = 2;
  binding.binding = 0;
  binding.type = ShaderPropertyType::UniformBuffer;
  binding.size = 32;

  auto shader =
      std::make_shared<FakeShader>(std::vector<ShaderResourceBinding>{binding});
  auto tmpl = MaterialTemplate::create("multi_pass_fake");

  ShaderProgramSet forwardSet;
  forwardSet.shaderName = "fake_forward";
  forwardSet.shader = shader;
  MaterialPassDefinition forwardEntry;
  forwardEntry.shaderProgram = forwardSet;
  forwardEntry.renderState = forwardState;
  tmpl->setPassDefinition(Pass_Forward, std::move(forwardEntry));

  ShaderProgramSet shadowSet;
  shadowSet.shaderName = "fake_shadow";
  shadowSet.shader = shader;
  MaterialPassDefinition shadowEntry;
  shadowEntry.shaderProgram = shadowSet;
  shadowEntry.renderState = shadowState;
  tmpl->setPassDefinition(Pass_Shadow, std::move(shadowEntry));

  tmpl->rebuildMaterialInterface();
  return tmpl;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_ubo_buffer_sized_from_reflection() {
  std::cout << "\n-- test_ubo_buffer_sized_from_reflection --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  const auto &buf = mat->getParameterBufferBytes();
  REQUIRE(!buf.empty());
  REQUIRE(mat->getParameterBufferLayout().has_value());
  // blinnphong_0 MaterialUBO: vec3(12) + float(4) + float(4) + 3*int(12) = 32
  REQUIRE(buf.size() == 32);
  std::cout << "  buffer size = " << buf.size() << "\n";
}

void test_setVec3_writes_12_bytes_only() {
  std::cout << "\n-- test_setVec3_writes_12_bytes_only --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  // Seed shininess first so we can verify setVec3 does not clobber it.
  mat->setParameter(StringID("MaterialUBO"), StringID("shininess"), 99.0f);
  const auto &buf = mat->getParameterBufferBytes();
  float shiny = 0.0f;
  std::memcpy(&shiny, buf.data() + 12, sizeof(float));
  REQUIRE(shiny == 99.0f);

  mat->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                    Vec3f{1.0f, 0.25f, 0.5f});
  float v0 = 0, v1 = 0, v2 = 0;
  std::memcpy(&v0, buf.data() + 0, sizeof(float));
  std::memcpy(&v1, buf.data() + 4, sizeof(float));
  std::memcpy(&v2, buf.data() + 8, sizeof(float));
  REQUIRE(v0 == 1.0f);
  REQUIRE(v1 == 0.25f);
  REQUIRE(v2 == 0.5f);

  // shininess at offset 12 must still be 99.0 — setVec3 wrote only 12 bytes.
  std::memcpy(&shiny, buf.data() + 12, sizeof(float));
  REQUIRE(shiny == 99.0f);
  std::cout << "  baseColor written, shininess preserved\n";
}

void test_setFloat_and_setInt_at_reflected_offsets() {
  std::cout << "\n-- test_setFloat_and_setInt_at_reflected_offsets --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;

  mat->setParameter(StringID("MaterialUBO"), StringID("specularIntensity"),
                    2.5f);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 1);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableNormal"), 0);

  const auto &buf = mat->getParameterBufferBytes();
  float spec = 0.0f;
  i32 ea = -1, en = -1;
  std::memcpy(&spec, buf.data() + 16, sizeof(float));
  std::memcpy(&ea, buf.data() + 20, sizeof(i32));
  std::memcpy(&en, buf.data() + 24, sizeof(i32));
  REQUIRE(spec == 2.5f);
  REQUIRE(ea == 1);
  REQUIRE(en == 0);
  std::cout << "  specularIntensity=2.5, enableAlbedo=1, enableNormal=0 OK\n";
}

void test_descriptor_resources_stable_ubo_identity() {
  std::cout << "\n-- test_descriptor_resources_stable_ubo_identity --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  auto a = mat->getDescriptorResources(Pass_Forward);
  auto b = mat->getDescriptorResources(Pass_Forward);
  REQUIRE(!a.empty());
  REQUIRE(!b.empty());
  REQUIRE(a[0].get() == b[0].get());
  REQUIRE(a[0]->getType() == ResourceType::UniformBuffer);
  REQUIRE(a[0]->getByteSize() == 32);
  std::cout << "  UBO IGpuResource identity stable\n";
}

void test_descriptor_resources_reflects_buffer_writes() {
  std::cout << "\n-- test_descriptor_resources_reflects_buffer_writes --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  mat->setParameter(StringID("MaterialUBO"), StringID("shininess"), 7.0f);
  auto resources = mat->getDescriptorResources(Pass_Forward);
  REQUIRE(!resources.empty());
  auto *raw = reinterpret_cast<const u8 *>(resources[0]->getRawData());
  float shiny = 0.0f;
  std::memcpy(&shiny, raw + 12, sizeof(float));
  REQUIRE(shiny == 7.0f);
  std::cout << "  wrapper reads live bytes\n";
}

void test_loader_produces_valid_instance() {
  std::cout << "\n-- test_loader_produces_valid_instance --\n";
  auto dir = findShaderDir();
  if (dir.empty()) {
    std::cerr << "  SETUP: skip\n";
    return;
  }
  // The loader walks the cwd upward for the shader dir, so chdir first.
  auto prev = std::filesystem::current_path();
  std::filesystem::current_path(dir.parent_path().parent_path());
  MaterialInstanceSharedPtr mat;
  try {
    mat = loadGenericMaterial("assets/materials/blinnphong_default.material");
  } catch (const std::exception &e) {
    std::cerr << "  FAIL: loader threw: " << e.what() << "\n";
    ++s_failures;
    std::filesystem::current_path(prev);
    return;
  }
  std::filesystem::current_path(prev);
  REQUIRE(mat != nullptr);
  REQUIRE(!mat->getParameterBufferBytes().empty());
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);

  // Seeded defaults: baseColor == {0.8, 0.8, 0.8}
  const auto &buf = mat->getParameterBufferBytes();
  float r = 0, g = 0, b = 0;
  std::memcpy(&r, buf.data() + 0, sizeof(float));
  std::memcpy(&g, buf.data() + 4, sizeof(float));
  std::memcpy(&b, buf.data() + 8, sizeof(float));
  REQUIRE(r == 0.8f);
  REQUIRE(g == 0.8f);
  REQUIRE(b == 0.8f);

  float shiny = 0;
  std::memcpy(&shiny, buf.data() + 12, sizeof(float));
  REQUIRE(shiny == 12.0f);
  std::cout << "  loader returned a seeded MaterialInstance\n";
}

void test_ubo_layout_comes_from_enabled_pass_shader() {
  std::cout << "\n-- test_ubo_layout_comes_from_enabled_pass_shader --\n";
  RenderState forwardState;
  RenderState shadowState;
  auto tmpl = buildMultiPassTemplate(forwardState, shadowState);
  auto mat = MaterialInstance::create(tmpl);
  REQUIRE(mat->getParameterBufferLayout().has_value());
  REQUIRE(mat->getParameterBufferBytes().size() == 32);
  std::cout << "  shared UBO layout accepted across all defined passes\n";
}

void test_instances_default_enable_all_template_passes() {
  std::cout << "\n-- test_instances_default_enable_all_template_passes --\n";
  RenderState forwardState;
  RenderState shadowState;
  auto tmpl = buildMultiPassTemplate(forwardState, shadowState);
  auto mat = MaterialInstance::create(tmpl);

  REQUIRE(mat->isPassEnabled(Pass_Forward));
  REQUIRE(mat->isPassEnabled(Pass_Shadow));
  REQUIRE(mat->getEnabledPasses().size() == 2);
  std::cout << "  new instances start with every template-defined pass enabled\n";
}

void test_enabled_passes_follow_mutations() {
  std::cout << "\n-- test_enabled_passes_follow_mutations --\n";
  RenderState forwardState;
  RenderState shadowState;
  auto tmpl = buildMultiPassTemplate(forwardState, shadowState);
  auto mat = MaterialInstance::create(tmpl);

  mat->setPassEnabled(Pass_Shadow, false);
  REQUIRE(mat->isPassEnabled(Pass_Forward));
  REQUIRE(!mat->isPassEnabled(Pass_Shadow));
  mat->setPassEnabled(Pass_Forward, false);
  REQUIRE(!mat->isPassEnabled(Pass_Forward));
  REQUIRE(mat->getEnabledPasses().empty());
  mat->setPassEnabled(Pass_Shadow, true);
  REQUIRE(!mat->isPassEnabled(Pass_Forward));
  REQUIRE(mat->isPassEnabled(Pass_Shadow));
  std::cout << "  enabled pass set follows the toggled subset only\n";
}

void test_render_state_is_pass_aware() {
  std::cout << "\n-- test_render_state_is_pass_aware --\n";
  RenderState forwardState;
  forwardState.cullMode = CullMode::Front;
  RenderState shadowState;
  shadowState.depthWriteEnable = false;
  shadowState.blendEnable = true;
  auto tmpl = buildMultiPassTemplate(forwardState, shadowState);
  auto mat = MaterialInstance::create(tmpl);

  REQUIRE(mat->getPassRenderState(Pass_Forward) == forwardState);
  REQUIRE(mat->getPassRenderState(Pass_Shadow) == shadowState);
  std::cout << "  render state is resolved from the queried pass entry\n";
}

void test_non_structural_writes_do_not_notify_pass_listeners() {
  std::cout << "\n-- test_non_structural_writes_do_not_notify_pass_listeners --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;

  int notifications = 0;
  const auto listenerId =
      mat->addPassStateListener([&notifications]() { ++notifications; });

  mat->setParameter(StringID("MaterialUBO"), StringID("shininess"), 7.0f);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 1);
  mat->syncGpuData();
  REQUIRE(notifications == 0);

  mat->setPassEnabled(Pass_Forward, false);
  REQUIRE(notifications == 1);
  mat->removePassStateListener(listenerId);
  std::cout << "  only structural pass changes notify listeners\n";
}

void test_setPassEnabled_throws_on_undefined_pass() {
  std::cout << "\n-- test_setPassEnabled_throws_on_undefined_pass --\n";
  RenderState forwardState;
  RenderState shadowState;
  auto tmpl = buildMultiPassTemplate(forwardState, shadowState);
  auto mat = MaterialInstance::create(tmpl);
  bool threw = false;
  try {
    mat->setPassEnabled(Pass_Deferred, false);
  } catch (const std::logic_error &) {
    threw = true;
  }
  REQUIRE(threw);
  std::cout << "  undefined pass throws logic_error\n";
}

void test_isSystemOwnedBinding_classification() {
  std::cout << "\n-- test_isSystemOwnedBinding_classification --\n";
  REQUIRE(isSystemOwnedBinding("CameraUBO") == true);
  REQUIRE(isSystemOwnedBinding("LightUBO") == true);
  REQUIRE(isSystemOwnedBinding("Bones") == true);
  REQUIRE(isSystemOwnedBinding("MaterialUBO") == false);
  REQUIRE(isSystemOwnedBinding("SurfaceParams") == false);
  REQUIRE(isSystemOwnedBinding("albedoMap") == false);
  REQUIRE(isSystemOwnedBinding("") == false);
  std::cout << "  ownership classification correct\n";
}

void test_material_instance_with_non_MaterialUBO_name() {
  std::cout << "\n-- test_material_instance_with_non_MaterialUBO_name --\n";

  // Build a shader with a UBO named "SurfaceParams" instead of "MaterialUBO".
  StructMemberInfo baseColor{"baseColor", ShaderPropertyType::Vec3, 0, 12};
  StructMemberInfo roughness{"roughness", ShaderPropertyType::Float, 12, 4};

  ShaderResourceBinding uboBinding;
  uboBinding.name = "SurfaceParams";
  uboBinding.set = 2;
  uboBinding.binding = 0;
  uboBinding.type = ShaderPropertyType::UniformBuffer;
  uboBinding.size = 16;
  uboBinding.members = {baseColor, roughness};

  ShaderResourceBinding cameraBinding;
  cameraBinding.name = "CameraUBO";
  cameraBinding.set = 0;
  cameraBinding.binding = 0;
  cameraBinding.type = ShaderPropertyType::UniformBuffer;
  cameraBinding.size = 144;

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{uboBinding, cameraBinding});
  auto tmpl = MaterialTemplate::create("surface_test");
  ShaderProgramSet set;
  set.shaderName = "surface_test";
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();

  auto mat = MaterialInstance::create(tmpl);

  // Buffer should be sized from SurfaceParams, not empty.
  REQUIRE(mat->getParameterBufferLayout().has_value());
  REQUIRE(mat->getParameterBufferBytes().size() == 16);

  // Setters should work by member name.
  mat->setParameter(StringID("SurfaceParams"), StringID("baseColor"),
                    Vec3f{0.5f, 0.6f, 0.7f});
  mat->setParameter(StringID("SurfaceParams"), StringID("roughness"), 0.3f);

  const auto &buf = mat->getParameterBufferBytes();
  float r = 0, g = 0, b = 0, rough = 0;
  std::memcpy(&r, buf.data() + 0, sizeof(float));
  std::memcpy(&g, buf.data() + 4, sizeof(float));
  std::memcpy(&b, buf.data() + 8, sizeof(float));
  std::memcpy(&rough, buf.data() + 12, sizeof(float));
  REQUIRE(r == 0.5f);
  REQUIRE(g == 0.6f);
  REQUIRE(b == 0.7f);
  REQUIRE(rough == 0.3f);

  // Descriptor resource should report "SurfaceParams", not "MaterialUBO".
  auto resources = mat->getDescriptorResources(Pass_Forward);
  REQUIRE(!resources.empty());
  REQUIRE(resources[0]->getBindingName() == StringID("SurfaceParams"));

  std::cout << "  SurfaceParams UBO works as material-owned binding\n";
}

void test_multi_buffer_setParameter() {
  std::cout << "\n-- test_multi_buffer_setParameter --\n";

  StructMemberInfo baseColor{"baseColor", ShaderPropertyType::Vec3, 0, 12};
  StructMemberInfo roughness{"roughness", ShaderPropertyType::Float, 12, 4};

  ShaderResourceBinding surfaceBinding;
  surfaceBinding.name = "SurfaceParams";
  surfaceBinding.set = 2;
  surfaceBinding.binding = 0;
  surfaceBinding.type = ShaderPropertyType::UniformBuffer;
  surfaceBinding.size = 16;
  surfaceBinding.members = {baseColor, roughness};

  StructMemberInfo detailScale{"detailScale", ShaderPropertyType::Float, 0, 4};
  StructMemberInfo detailOffset{"detailOffset", ShaderPropertyType::Float, 4, 4};

  ShaderResourceBinding detailBinding;
  detailBinding.name = "DetailParams";
  detailBinding.set = 2;
  detailBinding.binding = 1;
  detailBinding.type = ShaderPropertyType::UniformBuffer;
  detailBinding.size = 8;
  detailBinding.members = {detailScale, detailOffset};

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{surfaceBinding, detailBinding});
  auto tmpl = MaterialTemplate::create("multi_buffer");
  ShaderProgramSet set;
  set.shaderName = "multi_buffer";
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();

  auto mat = MaterialInstance::create(tmpl);
  REQUIRE(mat->getParameterBufferCount() == 2);

  // Write via setParameter (primary API).
  mat->setParameter(StringID("SurfaceParams"), StringID("roughness"), 0.8f);
  mat->setParameter(StringID("DetailParams"), StringID("detailScale"), 2.0f);

  const auto &surfBuf = mat->getParameterBufferBytes(StringID("SurfaceParams"));
  const auto &detBuf = mat->getParameterBufferBytes(StringID("DetailParams"));
  REQUIRE(surfBuf.size() == 16);
  REQUIRE(detBuf.size() == 8);

  float rough = 0, scale = 0;
  std::memcpy(&rough, surfBuf.data() + 12, sizeof(float));
  std::memcpy(&scale, detBuf.data() + 0, sizeof(float));
  REQUIRE(rough == 0.8f);
  REQUIRE(scale == 2.0f);

  std::cout << "  multi-buffer setParameter works independently\n";
}

void test_pass_aware_descriptor_resources() {
  std::cout << "\n-- test_pass_aware_descriptor_resources --\n";

  StructMemberInfo baseColor{"baseColor", ShaderPropertyType::Vec3, 0, 12};

  ShaderResourceBinding uboBinding;
  uboBinding.name = "MaterialUBO";
  uboBinding.set = 2;
  uboBinding.binding = 0;
  uboBinding.type = ShaderPropertyType::UniformBuffer;
  uboBinding.size = 12;
  uboBinding.members = {baseColor};

  ShaderResourceBinding texBinding;
  texBinding.name = "albedoMap";
  texBinding.set = 2;
  texBinding.binding = 1;
  texBinding.type = ShaderPropertyType::Texture2D;

  // Forward shader has UBO + texture.
  auto forwardShader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{uboBinding, texBinding});
  // Shadow shader has only UBO.
  auto shadowShader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{uboBinding});

  auto tmpl = MaterialTemplate::create("pass_test");

  ShaderProgramSet fwdSet;
  fwdSet.shaderName = "forward";
  fwdSet.shader = forwardShader;
  MaterialPassDefinition fwdEntry;
  fwdEntry.shaderProgram = fwdSet;
  fwdEntry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(fwdEntry));

  ShaderProgramSet shadSet;
  shadSet.shaderName = "shadow";
  shadSet.shader = shadowShader;
  MaterialPassDefinition shadEntry;
  shadEntry.shaderProgram = shadSet;
  shadEntry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Shadow, std::move(shadEntry));

  tmpl->rebuildMaterialInterface();

  auto mat = MaterialInstance::create(tmpl);
  // Don't bind a texture — forward should return only UBO, shadow too.
  auto fwdRes = mat->getDescriptorResources(Pass_Forward);
  auto shadRes = mat->getDescriptorResources(Pass_Shadow);
  // Both have the UBO (texture not bound so skipped).
  REQUIRE(fwdRes.size() == 1);
  REQUIRE(shadRes.size() == 1);
  REQUIRE(fwdRes[0]->getBindingName() == StringID("MaterialUBO"));
  REQUIRE(shadRes[0]->getBindingName() == StringID("MaterialUBO"));

  std::cout << "  pass-aware descriptor resources correct\n";
}

} // namespace

int main(int argc, char **argv) {
  expSetEnvVK();
  test_ubo_buffer_sized_from_reflection();
  test_setVec3_writes_12_bytes_only();
  test_setFloat_and_setInt_at_reflected_offsets();
  test_descriptor_resources_stable_ubo_identity();
  test_descriptor_resources_reflects_buffer_writes();
  test_loader_produces_valid_instance();
  test_ubo_layout_comes_from_enabled_pass_shader();
  test_instances_default_enable_all_template_passes();
  test_enabled_passes_follow_mutations();
  test_render_state_is_pass_aware();
  test_non_structural_writes_do_not_notify_pass_listeners();
  test_isSystemOwnedBinding_classification();
  test_material_instance_with_non_MaterialUBO_name();
  test_multi_buffer_setParameter();
  test_pass_aware_descriptor_resources();
  test_setPassEnabled_throws_on_undefined_pass();

  std::cout << "\n========================================\n";
  if (s_failures == 0) {
    std::cout << "test_material_instance: PASS\n";
  } else {
    std::cout << "test_material_instance: " << s_failures << " FAILURE(S)\n";
  }
  std::cout << "========================================\n";
  return s_failures;
}
