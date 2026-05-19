#include "core/asset/material_instance.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/texture_loader/placeholder_textures.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

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

namespace fs = std::filesystem;

class ScopedCurrentPath {
public:
  explicit ScopedCurrentPath(const fs::path &path)
      : m_previous(fs::current_path()) {
    fs::current_path(path);
  }

  ~ScopedCurrentPath() {
    std::error_code ec;
    fs::current_path(m_previous, ec);
  }

  ScopedCurrentPath(const ScopedCurrentPath &) = delete;
  ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;

private:
  fs::path m_previous;
};

class ScopedTempFile {
public:
  explicit ScopedTempFile(fs::path path) : m_path(std::move(path)) {}

  ~ScopedTempFile() {
    std::error_code ec;
    fs::remove(m_path, ec);
  }

  ScopedTempFile(const ScopedTempFile &) = delete;
  ScopedTempFile &operator=(const ScopedTempFile &) = delete;

private:
  fs::path m_path;
};

fs::path findProjectRoot() {
  fs::path cwd = fs::current_path();
  for (int i = 0; i < 5; ++i) {
    if (fs::exists(cwd / "assets" / "shaders" / "glsl" / "blinnphong_0.vert"))
      return cwd;
    auto parent = cwd.parent_path();
    if (parent == cwd)
      break;
    cwd = parent;
  }
  return {};
}

bool passHasEnabledVariant(const MaterialInstanceSharedPtr &mat, StringID pass,
                           const std::string &macroName) {
  const auto tmpl = mat ? mat->getTemplate() : nullptr;
  if (!tmpl) {
    return false;
  }
  const auto passDef = tmpl->getPassDefinition(pass);
  if (!passDef.has_value()) {
    return false;
  }
  for (const auto &variant : passDef->get().shaderProgram.variants) {
    if (variant.macroName == macroName) {
      return variant.enabled;
    }
  }
  return false;
}

void test_generic_loader_produces_valid_instance() {
  std::cout << "\n-- test_generic_loader_produces_valid_instance --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);

  auto matPath = root / "assets" / "materials" / "blinnphong_lit.material";
  if (!fs::exists(matPath)) {
    std::cerr << "  SETUP: material not found at " << matPath << "; skipping\n";
    fs::current_path(prev);
    return;
  }

  auto mat = loadGenericMaterial(matPath);
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->getParameterBufferCount() >= 1);
  REQUIRE(mat->getParameterBufferLayout().has_value());

  const auto &buf = mat->getParameterBufferBytes();
  REQUIRE(!buf.empty());

  float r = 0, g = 0, b = 0, shiny = 0;
  std::memcpy(&r, buf.data() + 0, sizeof(float));
  std::memcpy(&g, buf.data() + 4, sizeof(float));
  std::memcpy(&b, buf.data() + 8, sizeof(float));
  std::memcpy(&shiny, buf.data() + 12, sizeof(float));
  REQUIRE(r == 0.8f);
  REQUIRE(g == 0.8f);
  REQUIRE(b == 0.8f);
  REQUIRE(shiny == 12.0f);

  const auto debugShadowMode = mat->readParameterValue(
      StringID("MaterialUBO"), StringID("debugShadowMode"));
  REQUIRE(debugShadowMode.has_value());
  REQUIRE(debugShadowMode->type == MaterialParameterValueType::Int);
  REQUIRE(debugShadowMode->intValue == 0);

  std::cout << "  generic loader produced valid instance with correct defaults\n";
}

void test_flat_shading_model_enables_variant() {
  std::cout << "\n-- test_flat_shading_model_enables_variant --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath =
      root / "assets" / "materials" / "test_flat_shading_model.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "shadingModel: Flat\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.7, 0.7, 0.7]\n"
           "  MaterialUBO.shininess: 12.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n"
           "  MaterialUBO.debugShadowMode: 0\n";
  }

  MaterialInstanceSharedPtr mat;
  {
    ScopedCurrentPath currentPath(root);
    mat = loadGenericMaterial(matPath);
  }

  REQUIRE(mat != nullptr);
  REQUIRE(passHasEnabledVariant(mat, Pass_Forward, "USE_FLAT_SHADING"));

  std::cout << "  shadingModel Flat enables USE_FLAT_SHADING\n";
}

void test_default_shading_model_stays_smooth() {
  std::cout << "\n-- test_default_shading_model_stays_smooth --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(root / "assets" / "materials" /
                                 "blinnphong_lit.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(!passHasEnabledVariant(mat, Pass_Forward, "USE_FLAT_SHADING"));

  std::cout << "  missing shadingModel defaults to Smooth\n";
}

void test_smooth_shading_model_overrides_flat_variant() {
  std::cout << "\n-- test_smooth_shading_model_overrides_flat_variant --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath =
      root / "assets" / "materials" / "test_smooth_overrides_flat.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "shadingModel: Smooth\n"
           "variants:\n"
           "  USE_FLAT_SHADING: true\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.7, 0.7, 0.7]\n"
           "  MaterialUBO.shininess: 12.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n"
           "  MaterialUBO.debugShadowMode: 0\n";
  }

  MaterialInstanceSharedPtr mat;
  {
    ScopedCurrentPath currentPath(root);
    mat = loadGenericMaterial(matPath);
  }

  REQUIRE(mat != nullptr);
  REQUIRE(!passHasEnabledVariant(mat, Pass_Forward, "USE_FLAT_SHADING"));

  std::cout << "  shadingModel Smooth disables USE_FLAT_SHADING\n";
}

void test_invalid_shading_model_rejected() {
  std::cout << "\n-- test_invalid_shading_model_rejected --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath =
      root / "assets" / "materials" / "test_invalid_shading_model.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "shadingModel: Banana\n";
  }

  bool rejected = false;
  try {
    ScopedCurrentPath currentPath(root);
    (void)loadGenericMaterial(matPath);
  } catch (const std::logic_error &error) {
    rejected =
        std::string(error.what()).find("unknown shadingModel") !=
        std::string::npos;
  }

  REQUIRE(rejected);
  std::cout << "  invalid shadingModel rejected\n";
}

void test_mesh_overlay_material_metadata_loads() {
  std::cout << "\n-- test_mesh_overlay_material_metadata_loads --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath =
      root / "assets" / "materials" / "test_mesh_overlay_metadata.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "meshOverlay:\n"
           "  enabled: true\n"
           "  color: [0.1, 0.2, 0.3, 1.0]\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.7, 0.7, 0.7]\n"
           "  MaterialUBO.shininess: 12.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n"
           "  MaterialUBO.debugShadowMode: 0\n";
  }

  MaterialInstanceSharedPtr mat;
  {
    ScopedCurrentPath currentPath(root);
    mat = loadGenericMaterial(matPath);
  }

  REQUIRE(mat != nullptr);
  const auto tmpl = mat->getTemplate();
  REQUIRE(tmpl != nullptr);
  const auto passDef = tmpl->getPassDefinition(Pass_Forward);
  REQUIRE(passDef.has_value());

  const auto &meshOverlay = passDef->get().meshOverlay;
  REQUIRE(meshOverlay.enabled);
  REQUIRE(meshOverlay.color.x == 0.1f);
  REQUIRE(meshOverlay.color.y == 0.2f);
  REQUIRE(meshOverlay.color.z == 0.3f);
  REQUIRE(meshOverlay.color.w == 1.0f);

  std::cout << "  meshOverlay metadata loads into Pass_Forward\n";
}

bool meshOverlayColorRejectedWithMessage(const fs::path &root,
                                         const std::string &colorYaml) {
  const auto matPath =
      root / "assets" / "materials" / "test_invalid_mesh_overlay_color.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "meshOverlay:\n"
           "  enabled: true\n"
        << "  color: " << colorYaml << "\n";
  }

  bool rejected = false;
  try {
    ScopedCurrentPath currentPath(root);
    (void)loadGenericMaterial(matPath);
  } catch (const std::logic_error &error) {
    rejected =
        std::string(error.what()).find("meshOverlay.color requires 4 values") !=
        std::string::npos;
  }
  return rejected;
}

void test_invalid_mesh_overlay_color_rejected_with_loader_error() {
  std::cout
      << "\n-- test_invalid_mesh_overlay_color_rejected_with_loader_error --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  REQUIRE(meshOverlayColorRejectedWithMessage(root, "[1.0, 2.0, 3.0]"));
  REQUIRE(meshOverlayColorRejectedWithMessage(root, "1.0"));
  REQUIRE(meshOverlayColorRejectedWithMessage(root, "[1.0, nope, 3.0, 4.0]"));

  std::cout << "  invalid meshOverlay.color rejected through loader error\n";
}

void test_invalid_mesh_overlay_enabled_rejected_with_loader_error() {
  std::cout
      << "\n-- test_invalid_mesh_overlay_enabled_rejected_with_loader_error --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath = root / "assets" / "materials" /
                       "test_invalid_mesh_overlay_enabled.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "meshOverlay: { enabled: nope }\n";
  }

  bool rejected = false;
  try {
    ScopedCurrentPath currentPath(root);
    (void)loadGenericMaterial(matPath);
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what())
                   .find("meshOverlay.enabled requires a boolean") !=
               std::string::npos;
  }

  REQUIRE(rejected);
  std::cout << "  invalid meshOverlay.enabled rejected through loader error\n";
}

void test_pbr_example_material_loads() {
  std::cout << "\n-- test_pbr_example_material_loads --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);

  auto matPath = root / "assets" / "materials" / "pbr_gold.material";
  if (!fs::exists(matPath)) {
    std::cerr << "  SETUP: material not found at " << matPath << "; skipping\n";
    fs::current_path(prev);
    return;
  }

  auto mat = loadGenericMaterial(matPath);
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->isPassEnabled(Pass_Forward));
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);

  const auto &buf = mat->getParameterBufferBytes(StringID("MaterialUBO"));
  REQUIRE(buf.size() >= 28);

  float baseColor[4] = {};
  float metallic = 0.0f;
  float roughness = 0.0f;
  float ao = 0.0f;
  std::memcpy(baseColor, buf.data(), sizeof(baseColor));
  std::memcpy(&metallic, buf.data() + 16, sizeof(float));
  std::memcpy(&roughness, buf.data() + 20, sizeof(float));
  std::memcpy(&ao, buf.data() + 24, sizeof(float));

  REQUIRE(baseColor[0] == 1.0f);
  REQUIRE(baseColor[1] == 0.766f);
  REQUIRE(baseColor[2] == 0.336f);
  REQUIRE(baseColor[3] == 1.0f);
  REQUIRE(metallic == 1.0f);
  REQUIRE(roughness == 0.25f);
  REQUIRE(ao == 1.0f);

  auto resources = mat->getDescriptorResources(Pass_Forward);
  REQUIRE(resources.size() == 2);
  REQUIRE(resources[0]->getBindingName() == StringID("MaterialUBO"));
  REQUIRE(resources[1]->getBindingName() == StringID("albedoMap"));

  std::cout << "  pbr_gold.material loads through the formal asset path\n";
}

void test_rtr_experiment_template_material_loads() {
  std::cout << "\n-- test_rtr_experiment_template_material_loads --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(root / "assets" / "materials" /
                                 "rtr_experiment_template.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);
  REQUIRE(mat->findParameterMember(StringID("MaterialUBO"),
                                   StringID("surfaceColor"))
              .has_value());
  REQUIRE(mat->findParameterMember(StringID("MaterialUBO"),
                                   StringID("accentColor"))
              .has_value());
  REQUIRE(mat->findParameterMember(StringID("MaterialUBO"),
                                   StringID("mixAmount"))
              .has_value());
  REQUIRE(mat->findParameterMember(StringID("MaterialUBO"), StringID("mode"))
              .has_value());

  const auto mixAmount =
      mat->readParameterValue(StringID("MaterialUBO"), StringID("mixAmount"));
  REQUIRE(mixAmount.has_value());
  REQUIRE(mixAmount->type == MaterialParameterValueType::Float);
  REQUIRE(mixAmount->floatValue == 0.35f);

  std::cout << "  rtr_experiment_template.material loads and reflects params\n";
}

void test_rtr_shadertoy_quantum_core_material_loads() {
  std::cout << "\n-- test_rtr_shadertoy_quantum_core_material_loads --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(root / "assets" / "materials" /
                                 "rtr_shadertoy_quantum_core.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);
  REQUIRE(mat->findParameterMember(StringID("ShadertoyUBO"), StringID("time"))
              .has_value());
  REQUIRE(mat->findParameterMember(StringID("ShadertoyUBO"),
                                   StringID("resolution"))
              .has_value());
  REQUIRE(mat->findParameterMember(StringID("ShadertoyUBO"),
                                   StringID("audioBands"))
              .has_value());

  const auto time =
      mat->readParameterValue(StringID("ShadertoyUBO"), StringID("time"));
  REQUIRE(time.has_value());
  REQUIRE(time->type == MaterialParameterValueType::Float);
  REQUIRE(time->floatValue == 0.0f);

  const auto resolution = mat->readParameterValue(
      StringID("ShadertoyUBO"), StringID("resolution"));
  REQUIRE(resolution.has_value());
  REQUIRE(resolution->type == MaterialParameterValueType::Vec4);
  REQUIRE(resolution->vectorValue.x == 1280.0f);
  REQUIRE(resolution->vectorValue.y == 720.0f);

  const auto resources = mat->getDescriptorResources(Pass_Forward);
  const auto hasAudioChannel =
      std::any_of(resources.begin(), resources.end(), [](const auto &resource) {
        return resource && resource->getBindingName() == StringID("iChannel0");
      });
  REQUIRE(hasAudioChannel);

  std::cout << "  rtr_shadertoy_quantum_core.material loads and reflects params\n";
}

void test_per_pass_shader_override() {
  std::cout << "\n-- test_per_pass_shader_override --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto matPath = root / "assets" / "materials" / "test_per_pass_shader.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n\n"
           "variants:\n"
           "  USE_LIGHTING: true\n\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.5, 0.5, 0.5]\n"
           "  MaterialUBO.shininess: 8.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n\n"
           "passes:\n"
           "  Forward:\n"
           "    shader: blinnphong_0\n"
           "    variants:\n"
           "      USE_LIGHTING: true\n"
           "  Shadow:\n"
           "    shader: shadow_depth_only\n";
  }

  MaterialInstanceSharedPtr mat;
  {
    ScopedCurrentPath currentPath(root);
    mat = loadGenericMaterial(matPath);
  }

  REQUIRE(mat != nullptr);
  REQUIRE(mat->isPassEnabled(Pass_Forward));
  REQUIRE(mat->isPassEnabled(Pass_Shadow));

  // Both passes should have shader info.
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);
  REQUIRE(mat->getPassShader(Pass_Shadow) != nullptr);
  REQUIRE(mat->getDescriptorResources(Pass_Shadow).empty());

  std::cout << "  per-pass shader override works\n";
}

void test_canonical_parameters_shared_across_passes() {
  std::cout << "\n-- test_canonical_parameters_shared_across_passes --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto matPath = root / "assets" / "materials" / "test_pass_override.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n\n"
           "variants:\n"
           "  USE_LIGHTING: true\n\n"
           "parameters:\n"
           "  MaterialUBO.shininess: 4.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n\n"
           "passes:\n"
           "  Forward:\n"
           "    renderState:\n"
           "      depthTest: true\n"
           "  Shadow:\n"
           "    renderState:\n"
           "      depthTest: true\n";
  }

  MaterialInstanceSharedPtr mat;
  {
    ScopedCurrentPath currentPath(root);
    mat = loadGenericMaterial(matPath);
  }

  REQUIRE(mat != nullptr);

  const auto &globalBuf = mat->getParameterBufferBytes(StringID("MaterialUBO"));
  REQUIRE(globalBuf.size() >= 16);

  float globalShiny = 0;
  std::memcpy(&globalShiny, globalBuf.data() + 12, sizeof(float));
  REQUIRE(globalShiny == 4.0f);

  auto fwdRes = mat->getDescriptorResources(Pass_Forward);
  auto shadRes = mat->getDescriptorResources(Pass_Shadow);
  REQUIRE(!fwdRes.empty());
  REQUIRE(!shadRes.empty());
  REQUIRE(fwdRes[0]->getBindingName() == StringID("MaterialUBO"));
  REQUIRE(shadRes[0]->getBindingName() == StringID("MaterialUBO"));
  REQUIRE(fwdRes[0].get() == shadRes[0].get());

  std::cout << "  canonical parameters shared across passes\n";
}

void test_vector_parameters_load_without_aliasing_yaml_nodes() {
  std::cout << "\n-- test_vector_parameters_load_without_aliasing_yaml_nodes --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto matPath = root / "assets" / "materials" / "test_vector_params.material";
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: pbr\n\n"
           "parameters:\n"
           "  MaterialUBO.baseColorFactor: [0.25, 0.5, 0.75, 1.0]\n"
           "  MaterialUBO.metallicFactor: 0.9\n"
           "  MaterialUBO.roughnessFactor: 0.2\n"
           "  MaterialUBO.ao: 0.8\n"
           "resources:\n"
            "  albedoMap: white\n";
  }

  MaterialInstanceSharedPtr mat;
  {
    ScopedCurrentPath currentPath(root);
    mat = loadGenericMaterial(matPath);
  }

  REQUIRE(mat != nullptr);

  const auto &buf = mat->getParameterBufferBytes(StringID("MaterialUBO"));
  REQUIRE(buf.size() >= 28);

  float baseColor[4] = {};
  float metallic = 0.0f;
  float roughness = 0.0f;
  float ao = 0.0f;
  std::memcpy(baseColor, buf.data(), sizeof(baseColor));
  std::memcpy(&metallic, buf.data() + 16, sizeof(float));
  std::memcpy(&roughness, buf.data() + 20, sizeof(float));
  std::memcpy(&ao, buf.data() + 24, sizeof(float));

  REQUIRE(baseColor[0] == 0.25f);
  REQUIRE(baseColor[1] == 0.5f);
  REQUIRE(baseColor[2] == 0.75f);
  REQUIRE(baseColor[3] == 1.0f);
  REQUIRE(metallic == 0.9f);
  REQUIRE(roughness == 0.2f);
  REQUIRE(ao == 0.8f);

  std::cout << "  vector parameters survive YAML iteration safely\n";
}

void test_textured_character_material_has_projected_shadow_pass() {
  std::cout << "\n-- test_textured_character_material_has_projected_shadow_pass --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial("assets/materials/blinnphong_textured.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->isPassEnabled(Pass_Shadow));
  REQUIRE(mat->getPassShader(Pass_Shadow) != nullptr);
  REQUIRE(mat->getPassShader(Pass_Shadow)->getShaderName() ==
          "shadow_depth_only");
  REQUIRE(!mat->getPassRenderState(Pass_Shadow).blendEnable);
  REQUIRE(mat->getPassRenderState(Pass_Shadow).cullMode == CullMode::None);
  REQUIRE(mat->getPassRenderState(Pass_Shadow).depthWriteEnable);

  std::cout << "  textured character material exposes depth Shadow pass\n";
}

void test_placeholder_textures() {
  std::cout << "\n-- test_placeholder_textures --\n";

  auto white = getPlaceholderWhite();
  auto black = getPlaceholderBlack();
  auto normal = getPlaceholderNormal();

  REQUIRE(white != nullptr);
  REQUIRE(black != nullptr);
  REQUIRE(normal != nullptr);

  auto *wd = static_cast<const u8 *>(white->getRawData());
  REQUIRE(wd[0] == 255 && wd[1] == 255 && wd[2] == 255 && wd[3] == 255);

  auto *bd = static_cast<const u8 *>(black->getRawData());
  REQUIRE(bd[0] == 0 && bd[1] == 0 && bd[2] == 0 && bd[3] == 255);

  auto *nd = static_cast<const u8 *>(normal->getRawData());
  REQUIRE(nd[0] == 128 && nd[1] == 128 && nd[2] == 255 && nd[3] == 255);

  REQUIRE(getPlaceholderWhite().get() == white.get());
  REQUIRE(resolvePlaceholder("white").get() == white.get());
  REQUIRE(resolvePlaceholder("black").get() == black.get());
  REQUIRE(resolvePlaceholder("normal").get() == normal.get());
  REQUIRE(resolvePlaceholder("unknown") == nullptr);

  std::cout << "  placeholder textures correct\n";
}

} // namespace

int main() {
  expSetEnvVK();
  test_placeholder_textures();
  test_generic_loader_produces_valid_instance();
  test_flat_shading_model_enables_variant();
  test_default_shading_model_stays_smooth();
  test_smooth_shading_model_overrides_flat_variant();
  test_invalid_shading_model_rejected();
  test_mesh_overlay_material_metadata_loads();
  test_invalid_mesh_overlay_color_rejected_with_loader_error();
  test_invalid_mesh_overlay_enabled_rejected_with_loader_error();
  test_pbr_example_material_loads();
  test_rtr_experiment_template_material_loads();
  test_rtr_shadertoy_quantum_core_material_loads();
  test_per_pass_shader_override();
  test_canonical_parameters_shared_across_passes();
  test_vector_parameters_load_without_aliasing_yaml_nodes();
  test_textured_character_material_has_projected_shadow_pass();

  std::cout << "\n========================================\n";
  if (s_failures == 0) {
    std::cout << "test_generic_material_loader: PASS\n";
  } else {
    std::cout << "test_generic_material_loader: " << s_failures
              << " FAILURE(S)\n";
  }
  std::cout << "========================================\n";
  return s_failures;
}
