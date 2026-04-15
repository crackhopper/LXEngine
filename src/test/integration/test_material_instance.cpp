#include "core/asset/material.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/blinn_phong_material_loader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <cstdint>
#include <cstring>
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

MaterialInstance::Ptr
buildInstanceFromBlinnPhong(ResourcePassFlag flag = ResourcePassFlag::Forward) {
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
  auto shader = std::make_shared<CompiledShader>(std::move(compile.stages),
                                             bindings, "blinnphong_0");

  auto tmpl = MaterialTemplate::create("blinnphong_0", shader);
  ShaderProgramSet set;
  set.shaderName = "blinnphong_0";
  RenderPassEntry entry;
  entry.shaderSet = set;
  entry.renderState = RenderState{};
  entry.buildCache();
  tmpl->setPass(Pass_Forward, std::move(entry));
  tmpl->buildBindingCache();

  return MaterialInstance::create(tmpl, flag);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_ubo_buffer_sized_from_reflection() {
  std::cout << "\n-- test_ubo_buffer_sized_from_reflection --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  const auto &buf = mat->getUboBuffer();
  REQUIRE(!buf.empty());
  REQUIRE(mat->getUboBinding() != nullptr);
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
  mat->setFloat(StringID("shininess"), 99.0f);
  const auto &buf = mat->getUboBuffer();
  float shiny = 0.0f;
  std::memcpy(&shiny, buf.data() + 12, sizeof(float));
  REQUIRE(shiny == 99.0f);

  mat->setVec3(StringID("baseColor"), Vec3f{1.0f, 0.25f, 0.5f});
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

  mat->setFloat(StringID("specularIntensity"), 2.5f);
  mat->setInt(StringID("enableAlbedo"), 1);
  mat->setInt(StringID("enableNormal"), 0);

  const auto &buf = mat->getUboBuffer();
  float spec = 0.0f;
  int32_t ea = -1, en = -1;
  std::memcpy(&spec, buf.data() + 16, sizeof(float));
  std::memcpy(&ea, buf.data() + 20, sizeof(int32_t));
  std::memcpy(&en, buf.data() + 24, sizeof(int32_t));
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
  auto a = mat->getDescriptorResources();
  auto b = mat->getDescriptorResources();
  REQUIRE(!a.empty());
  REQUIRE(!b.empty());
  REQUIRE(a[0].get() == b[0].get());
  REQUIRE(a[0]->getType() == ResourceType::UniformBuffer);
  REQUIRE(a[0]->getByteSize() == 32);
  std::cout << "  UBO IRenderResource identity stable\n";
}

void test_descriptor_resources_reflects_buffer_writes() {
  std::cout << "\n-- test_descriptor_resources_reflects_buffer_writes --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  mat->setFloat(StringID("shininess"), 7.0f);
  auto resources = mat->getDescriptorResources();
  REQUIRE(!resources.empty());
  auto *raw = reinterpret_cast<const uint8_t *>(resources[0]->getRawData());
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
  MaterialInstance::Ptr mat;
  try {
    mat = loadBlinnPhongMaterial();
  } catch (const std::exception &e) {
    std::cerr << "  FAIL: loader threw: " << e.what() << "\n";
    ++s_failures;
    std::filesystem::current_path(prev);
    return;
  }
  std::filesystem::current_path(prev);
  REQUIRE(mat != nullptr);
  REQUIRE(!mat->getUboBuffer().empty());
  REQUIRE(mat->getShaderInfo() != nullptr);

  // Seeded defaults: baseColor == {0.8, 0.8, 0.8}
  const auto &buf = mat->getUboBuffer();
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

} // namespace

int main() {
  test_ubo_buffer_sized_from_reflection();
  test_setVec3_writes_12_bytes_only();
  test_setFloat_and_setInt_at_reflected_offsets();
  test_descriptor_resources_stable_ubo_identity();
  test_descriptor_resources_reflects_buffer_writes();
  test_loader_produces_valid_instance();

  std::cout << "\n========================================\n";
  if (s_failures == 0) {
    std::cout << "test_material_instance: PASS\n";
  } else {
    std::cout << "test_material_instance: " << s_failures << " FAILURE(S)\n";
  }
  std::cout << "========================================\n";
  return s_failures;
}
