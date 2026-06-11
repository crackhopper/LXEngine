#include "core/asset/material_instance.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_system_abi.hpp"
#include "core/scene/scene_system_abi_validation.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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
    for (const auto &candidate :
         {cwd / "assets" / "shaders" / "glsl", cwd / "shaders" / "glsl"}) {
      if (std::filesystem::exists(candidate / "techniques" / "Forward" /
                                  "pbr.vert") &&
          std::filesystem::exists(candidate / "techniques" / "Forward" /
                                  "pbr.frag"))
        return candidate;
    }
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
    std::cerr << "  SETUP: pbr shaders not found; skipping test\n";
    return nullptr;
  }
  auto compile = ShaderCompiler::compileProgram(
      dir / "techniques" / "Forward" / "pbr.vert",
      dir / "techniques" / "Forward" / "pbr.frag", {});
  if (!compile.success) {
    std::cerr << "  SETUP: compile failed: " << compile.errorMessage << "\n";
    return nullptr;
  }
  auto bindings = ShaderReflector::reflect(compile.stages);
  bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                [](const ShaderResourceBinding &binding) {
                                  return binding.name != "MaterialUBO" &&
                                         binding.name != "albedoMap";
                                }),
                 bindings.end());
  auto vertexInputs = ShaderReflector::reflectVertexInputs(compile.stages);
  auto shader = std::make_shared<CompiledShader>(
      std::move(compile.stages), bindings, vertexInputs, "pbr");

  auto tmpl = MaterialTemplate::create("pbr");
  ShaderProgramSet set;
  set.shaderName = "pbr";
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();

  return MaterialInstance::create(tmpl);
}

MaterialTemplate::SharedPtr
buildMultiPassTemplate(const RenderState &forwardState,
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

MaterialInstanceSharedPtr buildInstanceWithTextureBinding() {
  ShaderResourceBinding textureBinding;
  textureBinding.name = "albedoMap";
  textureBinding.set = 2;
  textureBinding.binding = 1;
  textureBinding.type = ShaderPropertyType::Texture2D;

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{textureBinding});
  auto tmpl = MaterialTemplate::create("texture_handle_fake");
  ShaderProgramSet set;
  set.shaderName = "texture_handle_fake";
  set.shader = shader;
  MaterialPassDefinition entry;
  entry.shaderProgram = set;
  entry.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(entry));
  tmpl->rebuildMaterialInterface();
  return MaterialInstance::create(tmpl);
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
  REQUIRE(mat->getParameterBufferLayout(StringID("MaterialUBO")).has_value());
  // pbr MaterialUBO:
  // vec3(12) + shininess(4) + specular(4) + ambient(4) + 3*int(12) = 36
  REQUIRE(buf.size() == 36);
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
  mat->setParameter(StringID("MaterialUBO"), StringID("ambientIntensity"),
                    0.0f);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableAlbedo"), 1);
  mat->setParameter(StringID("MaterialUBO"), StringID("enableNormal"), 0);

  const auto &buf = mat->getParameterBufferBytes();
  float spec = 0.0f, ambient = -1.0f;
  i32 ea = -1, en = -1;
  std::memcpy(&spec, buf.data() + 16, sizeof(float));
  std::memcpy(&ambient, buf.data() + 20, sizeof(float));
  std::memcpy(&ea, buf.data() + 24, sizeof(i32));
  std::memcpy(&en, buf.data() + 28, sizeof(i32));
  REQUIRE(spec == 2.5f);
  REQUIRE(ambient == 0.0f);
  REQUIRE(ea == 1);
  REQUIRE(en == 0);
  std::cout << "  specularIntensity=2.5, ambientIntensity=0, enableAlbedo=1, "
               "enableNormal=0 OK\n";
}

void test_parameter_resource_stable_ubo_identity() {
  std::cout << "\n-- test_parameter_resource_stable_ubo_identity --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  auto a = mat->getParameterResource(StringID("MaterialUBO"));
  auto b = mat->getParameterResource(StringID("MaterialUBO"));
  REQUIRE(a.isValid());
  REQUIRE(b.isValid());
  REQUIRE(&a.get() == &b.get());
  REQUIRE(a.get().getType() == ResourceType::UniformBuffer);
  REQUIRE(a.get().getByteSize() == 36);
  std::cout << "  UBO IGpuResource identity stable\n";
}

void test_parameter_resource_reflects_buffer_writes() {
  std::cout << "\n-- test_parameter_resource_reflects_buffer_writes --\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;
  mat->setParameter(StringID("MaterialUBO"), StringID("shininess"), 7.0f);
  auto resource = mat->getParameterResource(StringID("MaterialUBO"));
  REQUIRE(resource.isValid());
  auto *raw = reinterpret_cast<const u8 *>(resource.get().getRawData());
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
    mat = loadGenericMaterial("assets/materials/pbr.material");
  } catch (const std::exception &e) {
    std::cerr << "  FAIL: loader threw: " << e.what() << "\n";
    ++s_failures;
    std::filesystem::current_path(prev);
    return;
  }
  std::filesystem::current_path(prev);
  REQUIRE(mat != nullptr);
  REQUIRE(!mat->getParameterBufferBytes(StringID("MaterialUBO")).empty());
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);

  // Seeded defaults: baseColor == {0.8, 0.8, 0.8}
  const auto &buf = mat->getParameterBufferBytes(StringID("MaterialUBO"));
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
  std::cout
      << "  new instances start with every template-defined pass enabled\n";
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
  std::cout
      << "\n-- test_non_structural_writes_do_not_notify_pass_listeners --\n";
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

void test_material_state_version_tracks_parameter_and_handle_writes() {
  std::cout
      << "\n-- test_material_state_version_tracks_parameter_and_handle_writes "
         "--\n";
  auto mat = buildInstanceFromBlinnPhong();
  if (!mat)
    return;

  const u64 initialVersion = mat->getMaterialStateVersion();
  REQUIRE(!mat->hasPendingMaterialStateSync());

  mat->setParameter(StringID("MaterialUBO"), StringID("shininess"), 7.0f);
  REQUIRE(mat->getMaterialStateVersion() > initialVersion);
  REQUIRE(mat->hasPendingMaterialStateSync());

  mat->clearPendingMaterialStateSync();
  const u64 afterClearVersion = mat->getMaterialStateVersion();
  REQUIRE(!mat->hasPendingMaterialStateSync());

  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{0.25f, 0.5f, 0.75f};
  mat->setMaterialEnvelope(StringID("Kd"), kd);
  REQUIRE(mat->getMaterialStateVersion() > afterClearVersion);
  REQUIRE(mat->hasPendingMaterialStateSync());

  auto textureMat = buildInstanceWithTextureBinding();
  const u64 initialTextureVersion = textureMat->getMaterialStateVersion();
  REQUIRE(textureMat->getTemplate()
              ->findCanonicalMaterialBinding(StringID("albedoMap"))
              .has_value());
  TextureHandle textureHandle;
  textureHandle.index = 3;
  textureHandle.generation = 2;
  textureMat->setTextureHandle(StringID("albedoMap"), textureHandle);
  REQUIRE(textureMat->getMaterialStateVersion() > initialTextureVersion);
  REQUIRE(textureMat->hasPendingMaterialStateSync());

  mat->clearPendingMaterialStateSync();
  const u64 afterHandleVersion = mat->getMaterialStateVersion();
  MaterialResourceDependency dependency;
  dependency.kind = MaterialEnvelopeKind::Texture;
  dependency.parameterName = "Kd";
  dependency.uri = "memory://textures/shared.png";
  dependency.resourceHandle = ResourceIdentityHandle{5, 1};
  mat->addMaterialDependency(dependency);
  REQUIRE(mat->getMaterialStateVersion() > afterHandleVersion);
  REQUIRE(mat->hasPendingMaterialStateSync());

  const auto cloned = mat->cloneInstanceDataUnique();
  REQUIRE(cloned->getMaterialStateVersion() == mat->getMaterialStateVersion());
  REQUIRE(cloned->hasPendingMaterialStateSync() ==
          mat->hasPendingMaterialStateSync());

  std::cout << "  material state dirty/version tracks bindless-facing data\n";
}

void test_material_v2_envelope_storage_disables_parameter_buffers() {
  std::cout
      << "\n-- test_material_v2_envelope_storage_disables_parameter_buffers "
         "--\n";
  auto mat = MaterialInstance::create(
      buildMultiPassTemplate(RenderState{}, RenderState{}));

  REQUIRE(mat->getParameterBufferCount() >= 1);
  REQUIRE(mat->getParameterBufferLayout(StringID("MaterialUBO")).has_value());

  mat->setBsdfType("matte");
  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{0.25f, 0.5f, 0.75f};
  mat->setMaterialEnvelope(StringID("Kd"), kd);

  REQUIRE(mat->getMaterialEnvelope(StringID("Kd")).has_value());
  REQUIRE(mat->getParameterBufferCount() == 0);
  REQUIRE(!mat->getParameterBufferLayout(StringID("MaterialUBO")).has_value());
  REQUIRE(mat->getParameterBufferBytes(StringID("MaterialUBO")).empty());
  REQUIRE(!mat->getParameterResource(StringID("MaterialUBO")).isValid());
  REQUIRE(!mat->readParameterValue(StringID("MaterialUBO"),
                                   StringID("baseColorFactor"))
               .has_value());

  mat->setParameter(StringID("MaterialUBO"), StringID("baseColorFactor"),
                    Vec4f{1.0f, 0.5f, 0.25f, 1.0f});
  REQUIRE(!mat->readParameterValue(StringID("MaterialUBO"),
                                   StringID("baseColorFactor"))
               .has_value());
  REQUIRE(mat->getMaterialEnvelope(StringID("Kd")).has_value());

  const auto cloned = mat->cloneInstanceDataUnique();
  REQUIRE(cloned->getMaterialEnvelope(StringID("Kd")).has_value());
  REQUIRE(cloned->getParameterBufferCount() == 0);
  REQUIRE(!cloned->readParameterValue(StringID("MaterialUBO"),
                                      StringID("baseColorFactor"))
               .has_value());

  std::cout << "  material v2 keeps envelope truth without parameter buffers\n";
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
  REQUIRE(isSystemOwnedBinding("SceneCameraData") == true);
  REQUIRE(isSystemOwnedBinding("SceneLightData") == true);
  REQUIRE(isSystemOwnedBinding("SceneObjectData") == true);
  REQUIRE(isSystemOwnedBinding("SceneMaterialInstanceData") == true);
  REQUIRE(isSystemOwnedBinding("CameraUBO") == false);
  REQUIRE(isSystemOwnedBinding("LightUBO") == false);
  REQUIRE(isSystemOwnedBinding("SceneLightsUBO") == false);
  REQUIRE(isMaterialOwnedBinding("CameraUBO") == false);
  REQUIRE(isMaterialOwnedBinding("LightUBO") == false);
  REQUIRE(isMaterialOwnedBinding("SceneLightsUBO") == false);
  REQUIRE(isSystemOwnedBinding("BloomSource") == true);
  REQUIRE(isSystemOwnedBinding("BloomColor") == true);
  REQUIRE(isSystemOwnedBinding("SkyboxMap") == true);
  REQUIRE(isSystemOwnedBinding("IrradianceMap") == true);
  REQUIRE(isSystemOwnedBinding("PrefilteredEnvMap") == true);
  REQUIRE(isSystemOwnedBinding("BrdfLut") == true);
  REQUIRE(isSystemOwnedBinding("EnvironmentUBO") == false);
  REQUIRE(isSystemOwnedBinding("Bones") == false);
  REQUIRE(isMaterialOwnedBinding("EnvironmentUBO") == false);
  REQUIRE(isMaterialOwnedBinding("Bones") == false);
  REQUIRE(isSystemOwnedBinding("MaterialUBO") == false);
  REQUIRE(isSystemOwnedBinding("SurfaceParams") == false);
  REQUIRE(isSystemOwnedBinding("albedoMap") == false);
  REQUIRE(isSystemOwnedBinding("") == false);
  REQUIRE(getExpectedTypeForSystemBinding("IrradianceMap") ==
          ShaderPropertyType::TextureCube);
  REQUIRE(getExpectedTypeForSystemBinding("SkyboxMap") ==
          ShaderPropertyType::TextureCube);
  REQUIRE(getExpectedTypeForSystemBinding("BloomSource") ==
          ShaderPropertyType::Texture2D);
  REQUIRE(getExpectedTypeForSystemBinding("BloomColor") ==
          ShaderPropertyType::Texture2D);
  REQUIRE(getExpectedTypeForSystemBinding("PrefilteredEnvMap") ==
          ShaderPropertyType::TextureCube);
  REQUIRE(getExpectedTypeForSystemBinding("BrdfLut") ==
          ShaderPropertyType::Texture2D);
  REQUIRE(getExpectedTypeForSystemBinding("SceneCameraData") ==
          ShaderPropertyType::StorageBuffer);
  REQUIRE(getExpectedTypeForSystemBinding("SceneLightData") ==
          ShaderPropertyType::StorageBuffer);
  REQUIRE(getExpectedTypeForSystemBinding("SceneObjectData") ==
          ShaderPropertyType::StorageBuffer);
  REQUIRE(getExpectedTypeForSystemBinding("SceneMaterialInstanceData") ==
          ShaderPropertyType::StorageBuffer);
  REQUIRE(!getExpectedTypeForSystemBinding("EnvironmentUBO").has_value());
  std::cout << "  ownership classification correct\n";
}

void test_scene_system_abi_binding_contract_validation() {
  std::cout << "\n-- test_scene_system_abi_binding_contract_validation --\n";
  ShaderResourceBinding cameraBinding;
  cameraBinding.name = "SceneCameraData";
  cameraBinding.set = kSceneSystemDescriptorSet;
  cameraBinding.binding = kSceneSystemCameraBinding;
  cameraBinding.type = ShaderPropertyType::StorageBuffer;
  cameraBinding.size = sizeof(SceneSystemCameraData);
  cameraBinding.members = {
      StructMemberInfo{"view", ShaderPropertyType::Vec4,
                       static_cast<u32>(offsetof(SceneSystemCameraData, view)),
                       16},
      StructMemberInfo{
          "projection", ShaderPropertyType::Vec4,
          static_cast<u32>(offsetof(SceneSystemCameraData, projection)), 16},
      StructMemberInfo{"eye", ShaderPropertyType::Vec4,
                       static_cast<u32>(offsetof(SceneSystemCameraData, eye)),
                       16},
  };

  REQUIRE(!validateSystemAbiBindingContract(cameraBinding).has_value());

  auto wrongType = cameraBinding;
  wrongType.type = ShaderPropertyType::UniformBuffer;
  auto typeDiagnostic = validateSystemAbiBindingContract(wrongType);
  REQUIRE(typeDiagnostic.has_value());
  REQUIRE(typeDiagnostic->find("SceneCameraData") != std::string::npos);
  REQUIRE(typeDiagnostic->find("StorageBuffer") != std::string::npos);
  REQUIRE(typeDiagnostic->find("UniformBuffer") != std::string::npos);

  auto wrongBinding = cameraBinding;
  wrongBinding.binding = kSceneSystemLightBinding;
  auto bindingDiagnostic = validateSystemAbiBindingContract(wrongBinding);
  REQUIRE(bindingDiagnostic.has_value());
  REQUIRE(bindingDiagnostic->find("binding=0") != std::string::npos);
  REQUIRE(bindingDiagnostic->find("reflected set=0 binding=1") !=
          std::string::npos);

  auto wrongMember = cameraBinding;
  wrongMember.members[1].offset = 64;
  auto memberDiagnostic = validateSystemAbiBindingContract(wrongMember);
  REQUIRE(memberDiagnostic.has_value());
  REQUIRE(memberDiagnostic->find("projection") != std::string::npos);
  REQUIRE(memberDiagnostic->find("offset=16") != std::string::npos);
  REQUIRE(memberDiagnostic->find("reflected offset=64") != std::string::npos);

  std::cout << "  scene system ABI binding contract rejects drift\n";
}

void test_environment_data_setters_mark_dirty() {
  std::cout << "\n-- test_environment_data_setters_mark_dirty --\n";
  EnvironmentData data;
  REQUIRE(data.getIblIntensity() == 0.0f);
  REQUIRE(data.getPrefilteredMipCount() == 1.0f);
  REQUIRE(!data.isDirty());
  data.setParams(2.0f, 6.0f);
  REQUIRE(data.isDirty());
  REQUIRE(data.getIblIntensity() == 2.0f);
  REQUIRE(data.getPrefilteredMipCount() == 6.0f);
  std::cout << "  environment setters mark dirty\n";
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

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{uboBinding});
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
  REQUIRE(mat->getParameterBufferLayout(StringID("SurfaceParams")).has_value());
  REQUIRE(mat->getParameterBufferBytes(StringID("SurfaceParams")).size() == 16);

  // Setters should work by member name.
  mat->setParameter(StringID("SurfaceParams"), StringID("baseColor"),
                    Vec3f{0.5f, 0.6f, 0.7f});
  mat->setParameter(StringID("SurfaceParams"), StringID("roughness"), 0.3f);

  const auto &buf = mat->getParameterBufferBytes(StringID("SurfaceParams"));
  float r = 0, g = 0, b = 0, rough = 0;
  std::memcpy(&r, buf.data() + 0, sizeof(float));
  std::memcpy(&g, buf.data() + 4, sizeof(float));
  std::memcpy(&b, buf.data() + 8, sizeof(float));
  std::memcpy(&rough, buf.data() + 12, sizeof(float));
  REQUIRE(r == 0.5f);
  REQUIRE(g == 0.6f);
  REQUIRE(b == 0.7f);
  REQUIRE(rough == 0.3f);

  auto resource = mat->getParameterResource(StringID("SurfaceParams"));
  REQUIRE(resource.isValid());
  REQUIRE(resource.get().getBindingName() == StringID("SurfaceParams"));

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
  StructMemberInfo detailOffset{"detailOffset", ShaderPropertyType::Float, 4,
                                4};

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

void test_descriptor_resources_resolve_through_scene_resource_table() {
  std::cout
      << "\n-- test_descriptor_resources_resolve_through_scene_resource_table "
         "--\n";

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
  auto texture = std::make_shared<CombinedTextureSampler>(createWhiteTexture());
  texture->setBindingName(StringID("albedoMap"));
  mat->setTexture(StringID("albedoMap"), texture);

  auto scene = Scene::create(nullptr);
  const MaterialHandle materialHandle =
      scene->resources().registerMaterial(mat->cloneInstanceDataUnique());

  ValidatedRenderablePassData fwdRenderable;
  fwdRenderable.materialHandle = materialHandle;
  fwdRenderable.shaderInfo = forwardShader;
  auto fwdRes = buildSceneDescriptorResources(SceneDescriptorResourceContext{
      .scene = *scene,
      .renderable = fwdRenderable,
      .pass = Pass_Forward,
      .target = RenderTarget{},
      .sceneResources = {},
  });

  ValidatedRenderablePassData shadRenderable;
  shadRenderable.materialHandle = materialHandle;
  shadRenderable.shaderInfo = shadowShader;
  auto shadRes = buildSceneDescriptorResources(SceneDescriptorResourceContext{
      .scene = *scene,
      .renderable = shadRenderable,
      .pass = Pass_Shadow,
      .target = RenderTarget{},
      .sceneResources = {},
  });
  REQUIRE(fwdRes.size() == 2);
  REQUIRE(shadRes.size() == 1);
  REQUIRE(fwdRes[0].getBindingName() == StringID("MaterialUBO"));
  REQUIRE(fwdRes[1].getBindingName() == StringID("albedoMap"));
  REQUIRE(shadRes[0].getBindingName() == StringID("MaterialUBO"));

  std::cout
      << "  resolver descriptor resources come from scene resource table\n";
}

} // namespace

int main(int argc, char **argv) {
  expSetEnvVK();
  test_ubo_buffer_sized_from_reflection();
  test_setVec3_writes_12_bytes_only();
  test_setFloat_and_setInt_at_reflected_offsets();
  test_parameter_resource_stable_ubo_identity();
  test_parameter_resource_reflects_buffer_writes();
  test_loader_produces_valid_instance();
  test_ubo_layout_comes_from_enabled_pass_shader();
  test_instances_default_enable_all_template_passes();
  test_enabled_passes_follow_mutations();
  test_render_state_is_pass_aware();
  test_non_structural_writes_do_not_notify_pass_listeners();
  test_material_state_version_tracks_parameter_and_handle_writes();
  test_material_v2_envelope_storage_disables_parameter_buffers();
  test_isSystemOwnedBinding_classification();
  test_scene_system_abi_binding_contract_validation();
  test_environment_data_setters_mark_dirty();
  test_material_instance_with_non_MaterialUBO_name();
  test_multi_buffer_setParameter();
  test_descriptor_resources_resolve_through_scene_resource_table();
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
