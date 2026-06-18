#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/render_feature_shader_validation.hpp"
#include "core/frame_graph/render_path_feature_validation.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_resource_parser.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/resource_parsers/render_feature_resource_parser.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"
#include "infra/resource_parsers/texture_resource_parser.hpp"
#include "infra/scene_io/scene_document.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#ifndef LXE_SOURCE_DIR
#define LXE_SOURCE_DIR ""
#endif

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

bool renderPathGraphHasFeature(const LX_core::RenderPathGraph &graph,
                               const std::string &slot) {
  return std::any_of(
      graph.features.begin(), graph.features.end(),
      [&](const LX_core::RenderPathFeatureDependency &dependency) {
        return dependency.slot == slot;
      });
}

bool renderPassHasSource(const LX_core::RenderPassNode &pass,
                         const std::string &source) {
  return std::find(pass.sources.begin(), pass.sources.end(), source) !=
         pass.sources.end();
}

bool renderPathGraphHasPass(const LX_core::RenderPathGraph &graph,
                            const std::string &passId) {
  return std::any_of(graph.passes.begin(), graph.passes.end(),
                     [&](const LX_core::RenderPassNode &pass) {
                       return pass.id == passId;
                     });
}

class FakeShader final : public LX_core::IShader {
public:
  std::vector<LX_core::ShaderStageCode> stages;
  std::vector<LX_core::ShaderResourceBinding> bindings;
  std::vector<LX_core::ShaderSpecializationConstantInfo>
      specializationConstants;

  const std::vector<LX_core::ShaderStageCode> &getAllStages() const override {
    return stages;
  }

  const std::vector<LX_core::ShaderResourceBinding> &
  getReflectionBindings() const override {
    return bindings;
  }

  const std::vector<LX_core::ShaderSpecializationConstantInfo> &
  getSpecializationConstants() const override {
    return specializationConstants;
  }

  std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    const auto it = std::find_if(
        bindings.begin(), bindings.end(),
        [&](const LX_core::ShaderResourceBinding &candidate) {
          return candidate.set == set && candidate.binding == binding;
        });
    if (it == bindings.end()) {
      return std::nullopt;
    }
    return std::cref(*it);
  }

  std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    const auto it =
        std::find_if(bindings.begin(), bindings.end(),
                     [&](const LX_core::ShaderResourceBinding &candidate) {
                       return candidate.name == name;
                     });
    if (it == bindings.end()) {
      return std::nullopt;
    }
    return std::cref(*it);
  }

  usize getProgramHash() const override { return 0; }
};

template <typename ParsedResource>
bool hasDiagnosticContaining(const ParsedResource &parsed,
                             const std::string &needle) {
  for (const auto &diagnostic : parsed.diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string readTextFile(const std::string &path) {
  std::ifstream file(path);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

LX_infra::scene_io::SceneDocument parseSceneYaml(const std::string &fileName,
                                                 const std::string &contents) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / fileName;
  std::ofstream file(path);
  file << contents;
  file.close();
  return LX_infra::scene_io::loadSceneDocument(path);
}

template <typename Callable>
void expectThrowsContaining(Callable &&callable, const std::string &needle,
                            const std::string &message) {
  try {
    callable();
  } catch (const std::exception &error) {
    const std::string what = error.what();
    EXPECT(what.find(needle) != std::string::npos, message);
    return;
  }
  EXPECT(false, message);
}

bool legacySceneEnvironmentSatisfiesEnvironmentNode(
    const LX_infra::scene_io::SceneDocument &document) {
  const auto &rootNode = document.rootNode();
  return std::any_of(rootNode.children.begin(), rootNode.children.end(),
                     [](const LX_infra::scene_io::SceneNodeDocument &node) {
                       return node.environment.has_value();
                     });
}

LX_core::ShaderVariant parserTestMaterialContractSourceVariant() {
  return LX_core::ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .macroValue = "\"common/materials/standard_pbr.contract.glsl\"",
  };
}

std::optional<LX_core::IShaderSharedPtr>
compileShaderForFeatureValidation(const LX_core::RenderFeature &feature,
                                  std::vector<std::string> &diagnostics) {
  if (!feature.shader.has_value()) {
    diagnostics.push_back("feature has no shader URI");
    return std::nullopt;
  }

  LX_infra::CompileResult compiled;
  const std::string shaderUri = feature.shader->uri.string();
  if (feature.level == LX_core::RenderFeatureLevel::Pass &&
      shaderUri == "render_paths/Forward/pbr") {
    compiled = LX_infra::ShaderCompiler::compileProgram(
        "assets/shaders/glsl/render_paths/Forward/pbr.vert",
        "assets/shaders/glsl/render_paths/Forward/pbr.frag",
        {parserTestMaterialContractSourceVariant()});
  } else {
    compiled = LX_infra::ShaderCompiler::compileFile(
        std::filesystem::path("assets/shaders/glsl") / (shaderUri + ".frag"));
  }

  if (!compiled.success) {
    diagnostics.push_back("compile failed for " + shaderUri + ": " +
                          compiled.errorMessage);
    return std::nullopt;
  }

  auto bindings = LX_infra::ShaderReflector::reflect(compiled.stages);
  auto vertexInputs =
      LX_infra::ShaderReflector::reflectVertexInputs(compiled.stages);
  auto specializationConstants =
      LX_infra::ShaderReflector::reflectSpecializationConstants(
          compiled.stages);
  return std::make_shared<LX_infra::CompiledShader>(
      std::move(compiled.stages), std::move(bindings), std::move(vertexInputs),
      std::move(specializationConstants), shaderUri);
}

LX_core::ResourceUri writeTempRenderPathGraph(const std::string &fileName,
                                              const std::string &contents) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / fileName;
  std::ofstream file(path);
  file << contents;
  return LX_core::ResourceUri("file://" + path.generic_string());
}

void writeTextFile(const std::filesystem::path &path,
                   const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path);
  file << contents;
}

bool shaderHasCompiledPayload(const LX_core::ShaderResourceMetadata &shader) {
  return shader.payload && !shader.payload->getAllStages().empty() &&
         (!shader.payload->getReflectionBindings().empty() ||
          !shader.payload->getVertexInputs().empty());
}

void expectResolvedShaderDescriptor(
    const LX_core::ShaderResourceMetadata &shader,
    const std::string &contextMessage) {
  EXPECT(shader.sourceResolved,
         contextMessage + " should prove source resolution succeeded");
  EXPECT(!shader.sourceUris.empty(),
         contextMessage + " should retain source URI list");
  if (shader.requiresMaterialSourceVariant) {
    EXPECT(!shaderHasCompiledPayload(shader),
           contextMessage +
               " should defer compilation until material source is known");
    return;
  }
  EXPECT(shaderHasCompiledPayload(shader),
         contextMessage +
             " should retain live compiled shader payload and reflection data");
}

void testRemovedLegacyFiniteBoxRuntimeTokensDoNotAppearInProductionRuntime() {
  struct AuditCase {
    std::filesystem::path path;
    std::string token;
    const char *description;
  };

  const std::filesystem::path sourceRoot =
      std::filesystem::path(LXE_SOURCE_DIR);
  const std::filesystem::path repoRoot =
      sourceRoot.empty() ? std::filesystem::current_path() : sourceRoot;
  const AuditCase cases[] = {
      {"src/backend/vulkan/vulkan_realtime_renderer.cpp",
       "ensureEnvironmentBoxRenderable",
       "removed legacy runtime finite-box injection helper"},
      {"src/backend/vulkan/vulkan_realtime_renderer.cpp",
       "makeEnvironmentBoxMesh",
       "removed legacy runtime finite-box mesh helper"},
      {"src/backend/vulkan/vulkan_realtime_renderer.cpp",
       "__environment_finite_box",
       "removed legacy auto-added finite-box renderable"},
      {"src/backend/vulkan/vulkan_realtime_renderer.cpp", "finiteBoxBounds",
       "removed legacy runtime finite-box bounds lookup"},
      {"src/backend/vulkan/vulkan_realtime_renderer.cpp",
       "render_paths/Environment/environment_box",
       "removed legacy runtime material shader path"},
      {"src/core/frame_graph/render_work_compiler.cpp", "environment.box",
       "removed legacy finite-box renderable special ordering"},
      {"src/core/scene/scene_resource_table.cpp",
       "EnvironmentLightingFiniteBox",
       "removed legacy finite-box scene resource registration"},
      {"src/core/scene/scene_resource_table.cpp", "finiteBoxBounds",
       "removed legacy finite-box scene resource registration"},
      {"src/core/scene/ibl_environment.hpp", "EnvironmentLightingFiniteBoxUBO",
       "removed legacy finite-box UBO resource type"},
      {"src/core/scene/ibl_environment.hpp", "BackgroundMode",
       "removed legacy background-mode runtime state"},
      {"src/core/scene/scene_resource_table.cpp", "backgroundMode",
       "removed legacy background-mode scene resource registration"},
      {"src/core/scene/scene_resource_table.cpp", "finiteBox",
       "removed legacy finite-box scene resource registration"},
      {"assets/shaders/glsl/common/environment_lighting.glsl", "backgroundMode",
       "removed legacy background-mode shader ABI"},
      {"assets/shaders/glsl/render_paths/Skybox/skybox_background.frag",
       "backgroundMode", "removed legacy background-mode shader ABI"},
      {"src/core/asset/shader_binding_ownership.hpp",
       "EnvironmentLightingFiniteBoxUBO",
       "removed legacy finite-box system-owned shader binding"},
      {"assets/shaders/glsl/render_paths/Environment/environment_box.frag",
       "EnvironmentLightingFiniteBoxUBO",
       "removed legacy finite-box shader entry point"},
      {"assets/shaders/glsl/render_paths/Environment/environment_box.frag",
       "render_paths/Environment/environment_box",
       "removed legacy finite-box runtime material shader"},
  };

  for (const AuditCase &auditCase : cases) {
    const auto path = repoRoot / auditCase.path;
    if (!std::filesystem::exists(path)) {
      continue;
    }
    const std::string text = readTextFile(path.string());
    EXPECT(text.find(auditCase.token) == std::string::npos,
           std::string(auditCase.description) + " should not appear in " +
               auditCase.path.generic_string());
  }
}

void testRenderFeatureParsesPureEnvelope() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://shadow-feature", R"(
schema: lxe.render-feature.v1
name: MainShadow
feature: shadowmap
level: pass
shader:
  uri: render_paths/Forward/shadow_depth_only
parameters:
  resolution:
    kind: integer
    value: 2048
  bias:
    kind: float
    value: 0.001
)");

  EXPECT(parsed.renderFeature.has_value(), "pure render feature should parse");
  EXPECT(parsed.diagnostics.empty(),
         "pure render feature should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }

  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.name == "MainShadow", "feature name should be retained");
  EXPECT(feature.feature == "shadowmap", "feature kind should be retained");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Pass,
         "feature level should be retained");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri ==
                 LX_core::ResourceUri("render_paths/Forward/shadow_depth_only"),
         "shader ABI owner URI should be retained");
  EXPECT(feature.parameters.size() == 2, "parameters should be retained");
  EXPECT(feature.parameters.at("resolution").kind == "integer",
         "integer envelope kind should be retained");
  EXPECT(feature.parameters.at("resolution").value == "2048",
         "integer envelope value should be retained");
  EXPECT(feature.parameters.at("bias").value == "0.001",
         "float envelope value should be retained");
}

void testRenderFeatureParsesBindingMemberRequiredSchema() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-feature-schema", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    sourceHash: builtin-white-hash
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [0.08, 0.08, 0.10]
    binding: EnvironmentLightingUBO
    member: color
    required: true
)");

  EXPECT(parsed.renderFeature.has_value(),
         "feature binding/member/required schema should parse");
  EXPECT(parsed.diagnostics.empty(),
         "feature binding/member/required schema should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }

  const auto &feature = *parsed.renderFeature;
  const auto &environmentMap = feature.parameters.at("environmentMap");
  EXPECT(environmentMap.kind == "textureCube",
         "environmentMap kind should be retained");
  EXPECT(environmentMap.uri == LX_core::ResourceUri("builtin:env/white_cube"),
         "environmentMap uri should be retained");
  EXPECT(environmentMap.sourceHash == "builtin-white-hash",
         "environmentMap source hash should be retained");
  EXPECT(environmentMap.valueType == "linear-radiance",
         "environmentMap value type should be retained");
  EXPECT(environmentMap.binding == "SkyboxMap",
         "environmentMap binding should be retained");
  EXPECT(environmentMap.required,
         "environmentMap required flag should be retained");

  const auto &color = feature.parameters.at("color");
  EXPECT(color.binding == "EnvironmentLightingUBO",
         "UBO binding should be retained");
  EXPECT(color.member == "color", "UBO member should be retained");
  EXPECT(color.required, "UBO member required flag should be retained");
}

void testRenderFeatureParsesTextureCubeUriParameter() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://texture-cube-feature", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: EnvironmentLightingUBO
    member: intensity
    required: true
)");

  EXPECT(parsed.renderFeature.has_value(),
         "textureCube uri parameter should parse");
  EXPECT(parsed.diagnostics.empty(),
         "textureCube uri parameter should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }
  const auto &parameter = parsed.renderFeature->parameters.at("environmentMap");
  EXPECT(parameter.kind == "textureCube",
         "textureCube parameter kind should be retained");
  EXPECT(parameter.uri == LX_core::ResourceUri("builtin:env/white_cube"),
         "textureCube parameter uri should be retained");
  EXPECT(parameter.binding == "SkyboxMap",
         "textureCube parameter binding should be retained");
}

void testDefaultRenderFeatureAssetParses() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse(
      "assets/effects/tone_mapping.render-feature.yaml",
      readTextFile("assets/effects/tone_mapping.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(),
         "default tone mapping feature asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "default tone mapping feature asset should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }

  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.name == "ToneMapping",
         "default tone mapping feature should retain name");
  EXPECT(feature.feature == "toneMapping",
         "default tone mapping feature should retain feature kind");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Shader,
         "default tone mapping feature should be shader-level");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri ==
                 LX_core::ResourceUri("features/tone_mapping"),
         "default tone mapping feature should declare shader ABI owner");
  EXPECT(feature.parameters.find("exposure") != feature.parameters.end(),
         "default tone mapping feature should declare exposure");
  EXPECT(feature.parameters.find("mode") != feature.parameters.end(),
         "default tone mapping feature should declare mode");
  EXPECT(feature.parameters.size() == 2,
         "default tone mapping feature should only declare exposure and mode");
  EXPECT(feature.parameters.find("enabled") == feature.parameters.end(),
         "tone mapping feature must not own enable_tonemapping");
  EXPECT(feature.parameters.find("gamma") == feature.parameters.end(),
         "tone mapping feature must not expose a gamma parameter");
}

void testEnvironmentLightingRenderFeatureAssetParses() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse(
      "assets/effects/environment_lighting.render-feature.yaml",
      readTextFile("assets/effects/environment_lighting.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(),
         "environment lighting feature asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "environment lighting feature asset should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }

  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.feature == "environmentLighting",
         "environment lighting feature kind should be retained");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Shader,
         "environment lighting feature should be shader-level");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri ==
                 LX_core::ResourceUri("features/environment_lighting"),
         "environment lighting feature should declare shader ABI owner");
  EXPECT(feature.parameters.size() == 4,
         "environment lighting feature should only declare IBL parameters");
  for (const char *name :
       {"environmentMap", "color", "intensity", "rotation"}) {
    EXPECT(feature.parameters.find(name) != feature.parameters.end(),
           std::string("environment lighting feature should declare ") + name);
  }
  EXPECT(feature.parameters.find("visibleInBackground") ==
             feature.parameters.end(),
         "environment lighting feature must hard-cut visibleInBackground");
  EXPECT(feature.parameters.find("backgroundMode") == feature.parameters.end(),
         "environment lighting feature must not own visible background mode");
  EXPECT(feature.parameters.find("finiteBoxBounds") == feature.parameters.end(),
         "environment lighting feature must not own finite box bounds");
  EXPECT(feature.parameters.find("skyboxEnabled") == feature.parameters.end(),
         "environment lighting feature must not declare skyboxEnabled");
  EXPECT(feature.parameters.find("ambientColor") == feature.parameters.end(),
         "environment lighting feature must not declare ambientColor");
  EXPECT(feature.parameters.find("ambientIntensity") ==
             feature.parameters.end(),
         "environment lighting feature must not declare ambientIntensity");
}

void testEnvironmentLightingRegistrationUsesCurrentFeatureParameters() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse(
      "assets/effects/environment_lighting.render-feature.yaml",
      readTextFile("assets/effects/environment_lighting.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(),
         "environment lighting feature asset should parse before registration");
  if (!parsed.renderFeature.has_value()) {
    return;
  }

  LX_core::SceneResourceTable table;
  LX_core::RenderFeature feature = *parsed.renderFeature;
  feature.parameters.at("environmentMap").uri =
      LX_core::ResourceUri("builtin:env/white_cube");
  const LX_core::RenderFeatureHandle handle = table.registerRenderFeature(
      LX_core::ResourceUri(
          "assets/effects/environment_lighting.render-feature.yaml"),
      std::move(feature));
  EXPECT(handle.isValid(),
         "environment lighting feature payload should register");

  const auto resources = table.getEnvironmentLightingResources();
  const auto hasBinding = [&](LX_core::StringID bindingName) {
    return std::any_of(resources.begin(), resources.end(),
                       [&](const LX_core::GpuResourceRef &resource) {
                         return resource.isValid() &&
                                resource.getBindingName() == bindingName;
                       });
  };

  EXPECT(hasBinding(LX_core::StringID("SkyboxMap")),
         "current environment lighting feature should register SkyboxMap");
  EXPECT(hasBinding(LX_core::StringID("EnvironmentLightingUBO")),
         "current environment lighting feature should register "
         "EnvironmentLightingUBO without a backgroundMode parameter");
}

void testForwardPassRenderFeatureAssetParses() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse(
      "assets/effects/forward_pass.render-feature.yaml",
      readTextFile("assets/effects/forward_pass.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(),
         "forwardPass feature asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "forwardPass feature asset should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }
  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.feature == "forwardPass",
         "forwardPass feature kind should be retained");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Pass,
         "forwardPass should be pass-level");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri ==
                 LX_core::ResourceUri("render_paths/Forward/pbr"),
         "forwardPass should declare Forward pbr shader ABI owner");
  EXPECT(feature.parameters.size() == 6,
         "forwardPass should declare flow switches and volatile IBL fields");
  for (const char *name :
       {"render_skybox", "enable_tonemapping", "enable_gamma"}) {
    EXPECT(feature.parameters.find(name) != feature.parameters.end(),
           std::string("forwardPass should declare ") + name);
    if (feature.parameters.find(name) != feature.parameters.end()) {
      EXPECT(!feature.parameters.at(name).volatileRuntime,
             std::string("non-volatile pass parameter expected: ") + name);
      EXPECT(feature.parameters.at(name).binding.empty(),
             std::string("pass parameter must not declare binding: ") + name);
      EXPECT(feature.parameters.at(name).member.empty(),
             std::string("pass parameter must not declare member: ") + name);
    }
  }
  for (const char *name :
       {"enableIblLighting", "environmentIblReady", "standardPbrIblReady"}) {
    EXPECT(feature.parameters.find(name) != feature.parameters.end(),
           std::string("forwardPass should declare volatile field ") + name);
    if (feature.parameters.find(name) != feature.parameters.end()) {
      const auto &parameter = feature.parameters.at(name);
      EXPECT(parameter.volatileRuntime,
             std::string("IBL pass field should be volatile: ") + name);
      EXPECT(parameter.binding == "PassRuntimeUBO",
             std::string("IBL pass field should use PassRuntimeUBO: ") + name);
      EXPECT(parameter.member == name,
             std::string("IBL pass field should map to same UBO member: ") +
                 name);
      EXPECT(parameter.value.empty(),
             std::string("volatile IBL pass field must not define value: ") +
                 name);
    }
  }
}

void testSkyboxRenderFeatureAssetParses() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed =
      parser.parse("assets/effects/skybox.render-feature.yaml",
                   readTextFile("assets/effects/skybox.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(), "skybox feature asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "skybox feature asset should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }
  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.feature == "skybox", "skybox feature kind should be retained");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Shader,
         "skybox should be shader-level");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri == LX_core::ResourceUri("features/skybox"),
         "skybox should declare shader ABI owner");
  for (const char *name :
       {"environmentMap", "color", "intensity", "rotation"}) {
    EXPECT(feature.parameters.find(name) != feature.parameters.end(),
           std::string("skybox should declare ") + name);
  }
  EXPECT(feature.parameters.find("finiteBoxBounds") == feature.parameters.end(),
         "skybox feature must not declare finite box bounds");
  EXPECT(feature.parameters.find("backgroundMode") == feature.parameters.end(),
         "skybox feature must not declare background mode");
}

void testBloomRenderFeatureAssetParses() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed =
      parser.parse("assets/effects/bloom.render-feature.yaml",
                   readTextFile("assets/effects/bloom.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(), "bloom feature asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "bloom feature asset should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }
  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.feature == "bloom", "bloom feature kind should be retained");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Shader,
         "bloom should be shader-level");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri == LX_core::ResourceUri("features/bloom"),
         "bloom should declare shader ABI owner");
  EXPECT(feature.parameters.size() == 3,
         "bloom should only declare threshold, intensity, and radius");
  for (const char *name : {"threshold", "intensity", "radius"}) {
    EXPECT(feature.parameters.find(name) != feature.parameters.end(),
           std::string("bloom should declare ") + name);
  }
}

void testSurfaceLightingRenderFeatureAssetParses() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse(
      "assets/effects/surface_lighting.render-feature.yaml",
      readTextFile("assets/effects/surface_lighting.render-feature.yaml"));

  EXPECT(parsed.renderFeature.has_value(),
         "surface lighting feature asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "surface lighting feature asset should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }

  const auto &feature = *parsed.renderFeature;
  EXPECT(feature.feature == "surfaceLighting",
         "surface lighting feature kind should be retained");
  EXPECT(feature.level == LX_core::RenderFeatureLevel::Shader,
         "surface lighting should be shader-level so Forward and Deferred can "
         "share one payload");
  EXPECT(feature.shader.has_value() &&
             feature.shader->uri ==
                 LX_core::ResourceUri("features/surface_lighting"),
         "surface lighting feature should declare a shared shader ABI owner");
  EXPECT(feature.parameters.size() == 5,
         "surface lighting should only declare IBL runtime fields");

  const auto expectParameter = [&](const char *name, const char *kind) {
    const auto parameter = feature.parameters.find(name);
    EXPECT(parameter != feature.parameters.end(),
           std::string("surface lighting should declare ") + name);
    if (parameter == feature.parameters.end()) {
      return;
    }
    EXPECT(parameter->second.kind == kind,
           std::string("surface lighting kind mismatch for ") + name);
    EXPECT(parameter->second.binding == "SurfaceLightingUBO",
           std::string("surface lighting binding mismatch for ") + name);
    EXPECT(parameter->second.member == name,
           std::string("surface lighting member mismatch for ") + name);
    EXPECT(parameter->second.required,
           std::string("surface lighting required flag missing for ") + name);
    EXPECT(!parameter->second.volatileRuntime,
           std::string("surfaceLighting is a live feature payload, not a "
                       "volatile pass field: ") +
               name);
  };

  expectParameter("enableIblLighting", "bool");
  expectParameter("diffuseIblIntensity", "float");
  expectParameter("specularIblIntensity", "float");
  expectParameter("environmentIblReady", "bool");
  expectParameter("standardPbrIblReady", "bool");
}

void testRenderFeatureShaderAbiValidationAcceptsShaderBindingsAndMembers() {
  LX_core::RenderFeature feature;
  feature.name = "Skybox";
  feature.feature = "skybox";
  feature.level = LX_core::RenderFeatureLevel::Shader;
  feature.shader = LX_core::RenderFeatureShaderContract{
      LX_core::ResourceUri("features/skybox")};
  feature.parameters["environmentMap"] = LX_core::RenderFeatureParameter{
      .kind = "textureCube",
      .uri = LX_core::ResourceUri("builtin:env/white_cube"),
      .binding = "SkyboxMap",
      .required = true,
  };
  feature.parameters["color"] = LX_core::RenderFeatureParameter{
      .kind = "vec3",
      .value = "[1.0, 1.0, 1.0]",
      .binding = "SkyboxUBO",
      .member = "color",
      .required = true,
  };

  FakeShader shader;
  shader.bindings = {
      LX_core::ShaderResourceBinding{
          .name = "SkyboxMap",
          .set = 1,
          .binding = 0,
          .type = LX_core::ShaderPropertyType::TextureCube,
      },
      LX_core::ShaderResourceBinding{
          .name = "SkyboxUBO",
          .set = 1,
          .binding = 1,
          .type = LX_core::ShaderPropertyType::UniformBuffer,
          .members = {LX_core::StructMemberInfo{
              "color", LX_core::ShaderPropertyType::Vec3, 0, 12}},
      },
  };

  const auto diagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(diagnostics.empty(),
         "shader-level feature should validate reflected texture and UBO "
         "member ABI");

  shader.bindings[0].type = LX_core::ShaderPropertyType::Texture2D;
  const auto textureDiagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(!textureDiagnostics.empty(),
         "textureCube parameter should reject reflected Texture2D binding");
}

void testSurfaceLightingShaderAbiValidationAcceptsSharedUboMembers() {
  LX_core::RenderFeature feature;
  feature.name = "SurfaceLighting";
  feature.feature = "surfaceLighting";
  feature.level = LX_core::RenderFeatureLevel::Shader;
  feature.shader = LX_core::RenderFeatureShaderContract{
      LX_core::ResourceUri("features/surface_lighting")};
  feature.parameters["enableIblLighting"] = LX_core::RenderFeatureParameter{
      .kind = "bool",
      .value = "true",
      .binding = "SurfaceLightingUBO",
      .member = "enableIblLighting",
      .required = true,
  };
  feature.parameters["diffuseIblIntensity"] = LX_core::RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "SurfaceLightingUBO",
      .member = "diffuseIblIntensity",
      .required = true,
  };
  feature.parameters["specularIblIntensity"] = LX_core::RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "SurfaceLightingUBO",
      .member = "specularIblIntensity",
      .required = true,
  };
  feature.parameters["environmentIblReady"] = LX_core::RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
      .binding = "SurfaceLightingUBO",
      .member = "environmentIblReady",
      .required = true,
  };
  feature.parameters["standardPbrIblReady"] = LX_core::RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
      .binding = "SurfaceLightingUBO",
      .member = "standardPbrIblReady",
      .required = true,
  };

  FakeShader shader;
  shader.bindings = {
      LX_core::ShaderResourceBinding{
          .name = "SurfaceLightingUBO",
          .set = 4,
          .binding = 2,
          .type = LX_core::ShaderPropertyType::UniformBuffer,
          .members =
              {LX_core::StructMemberInfo{
                   "enableIblLighting", LX_core::ShaderPropertyType::Int, 0, 4},
               LX_core::StructMemberInfo{"diffuseIblIntensity",
                                         LX_core::ShaderPropertyType::Float, 4,
                                         4},
               LX_core::StructMemberInfo{"specularIblIntensity",
                                         LX_core::ShaderPropertyType::Float, 8,
                                         4},
               LX_core::StructMemberInfo{"environmentIblReady",
                                         LX_core::ShaderPropertyType::Int, 12,
                                         4},
               LX_core::StructMemberInfo{"standardPbrIblReady",
                                         LX_core::ShaderPropertyType::Int, 16,
                                         4}},
      },
  };

  const auto diagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(diagnostics.empty(),
         "surfaceLighting should validate the reflected shared UBO ABI");
}

void testRenderFeatureShaderAbiValidationAcceptsPassSpecializationConstants() {
  LX_core::RenderFeature feature;
  feature.name = "ForwardPass";
  feature.feature = "forwardPass";
  feature.level = LX_core::RenderFeatureLevel::Pass;
  feature.shader = LX_core::RenderFeatureShaderContract{
      LX_core::ResourceUri("render_paths/Forward/pbr")};
  feature.parameters["render_skybox"] = LX_core::RenderFeatureParameter{
      .kind = "bool",
      .value = "true",
      .required = true,
  };

  FakeShader shader;
  shader.specializationConstants = {
      LX_core::ShaderSpecializationConstantInfo{
          .name = "render_skybox",
          .stage = LX_core::ShaderStage::Fragment,
          .constantId = 17,
          .type = LX_core::ShaderSpecializationValueType::Bool,
      },
  };

  const auto diagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(diagnostics.empty(),
         "pass-level feature should validate reflected specialization "
         "constant by name and type");

  feature.parameters["render_skybox"].kind = "float";
  const auto typeDiagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(!typeDiagnostics.empty(),
         "pass-level feature should reject specialization type mismatch");
}

void testRenderFeatureShaderAbiValidationAcceptsVolatilePassUniformMembers() {
  LX_core::RenderFeature feature;
  feature.name = "ForwardPass";
  feature.feature = "forwardPass";
  feature.level = LX_core::RenderFeatureLevel::Pass;
  feature.shader = LX_core::RenderFeatureShaderContract{
      LX_core::ResourceUri("render_paths/Forward/pbr")};
  feature.parameters["enableIblLighting"] = LX_core::RenderFeatureParameter{
      .kind = "bool",
      .binding = "PassRuntimeUBO",
      .member = "enableIblLighting",
      .required = true,
      .volatileRuntime = true,
  };

  FakeShader shader;
  shader.bindings = {
      LX_core::ShaderResourceBinding{
          .name = "PassRuntimeUBO",
          .set = 4,
          .binding = 1,
          .type = LX_core::ShaderPropertyType::UniformBuffer,
          .members = {LX_core::StructMemberInfo{
              "enableIblLighting", LX_core::ShaderPropertyType::Int, 0, 4}},
      },
  };

  const auto diagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(
      diagnostics.empty(),
      "volatile pass-level feature should validate reflected UBO member ABI");

  shader.bindings[0].members.clear();
  const auto missingMemberDiagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, shader);
  EXPECT(!missingMemberDiagnostics.empty(),
         "volatile pass-level feature should reject missing UBO member");

  FakeShader missingPassRuntimeShader;
  const auto deferredPassRuntimeDiagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature,
                                              missingPassRuntimeShader);
  EXPECT(deferredPassRuntimeDiagnostics.empty(),
         "missing PassRuntimeUBO is temporarily deferred until Task 14 shader "
         "ABI lands");

  feature.parameters["enableIblLighting"].binding = "OtherRuntimeUBO";
  const auto missingOtherRuntimeDiagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature,
                                              missingPassRuntimeShader);
  EXPECT(!missingOtherRuntimeDiagnostics.empty(),
         "missing non-PassRuntimeUBO volatile binding should fail ABI "
         "validation");

  FakeShader wrongBindingTypeShader;
  wrongBindingTypeShader.bindings = {
      LX_core::ShaderResourceBinding{
          .name = "OtherRuntimeUBO",
          .set = 4,
          .binding = 2,
          .type = LX_core::ShaderPropertyType::Texture2D,
      },
  };
  const auto wrongBindingTypeDiagnostics =
      LX_core::validateRenderFeatureShaderAbi(feature, wrongBindingTypeShader);
  EXPECT(!wrongBindingTypeDiagnostics.empty(),
         "volatile pass-level feature should reject reflected non-UBO binding");
}

void testProductionRenderFeatureAssetsValidateShaderAbi() {
  const char *assets[] = {
      "assets/effects/forward_pass.render-feature.yaml",
      "assets/effects/skybox.render-feature.yaml",
      "assets/effects/environment_lighting.render-feature.yaml",
      "assets/effects/tone_mapping.render-feature.yaml",
      "assets/effects/bloom.render-feature.yaml",
  };

  LX_infra::RenderFeatureResourceParser parser;
  for (const char *asset : assets) {
    const auto parsed = parser.parse(asset, readTextFile(asset));
    if (!parsed.diagnostics.empty()) {
      for (const std::string &diagnostic : parsed.diagnostics) {
        std::cerr << "[diag] " << diagnostic << '\n';
      }
    }
    EXPECT(parsed.renderFeature.has_value(),
           std::string(asset) + " should parse before ABI validation");
    EXPECT(parsed.diagnostics.empty(),
           std::string(asset) + " should parse without diagnostics");
    if (!parsed.renderFeature.has_value()) {
      continue;
    }

    std::vector<std::string> compileDiagnostics;
    const auto shader = compileShaderForFeatureValidation(*parsed.renderFeature,
                                                          compileDiagnostics);
    if (!compileDiagnostics.empty()) {
      for (const std::string &diagnostic : compileDiagnostics) {
        std::cerr << "[diag] " << asset << ": " << diagnostic << '\n';
      }
    }
    EXPECT(shader.has_value(),
           std::string(asset) + " shader ABI owner should compile");
    if (!shader.has_value()) {
      continue;
    }

    const auto abiDiagnostics = LX_core::validateRenderFeatureShaderAbi(
        *parsed.renderFeature, **shader);
    if (!abiDiagnostics.empty()) {
      for (const auto &diagnostic : abiDiagnostics) {
        std::cerr << "[diag] " << asset << ": " << diagnostic.parameter << ": "
                  << diagnostic.message << '\n';
      }
    }
    EXPECT(abiDiagnostics.empty(),
           std::string(asset) +
               " should validate against reflected shader ABI");
  }
}

void testRenderFeatureRejectsMissingLevel() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-missing-level", R"(
schema: lxe.render-feature.v1
name: MissingLevel
feature: missingLevel
shader:
  uri: features/test
parameters:
  enabled:
    kind: bool
    value: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "render feature without level should fail");
  EXPECT(hasDiagnosticContaining(parsed, "level"),
         "diagnostic should name missing level");
}

void testRenderFeatureRejectsInvalidLevel() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-invalid-level", R"(
schema: lxe.render-feature.v1
name: InvalidLevel
feature: invalidLevel
level: technique
shader:
  uri: features/test
parameters:
  enabled:
    kind: bool
    value: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "render feature with invalid level should fail");
  EXPECT(hasDiagnosticContaining(parsed, "level"),
         "diagnostic should name invalid level");
}

void testRenderFeatureRejectsMalformedShader() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-malformed-shader", R"(
schema: lxe.render-feature.v1
name: MalformedShader
feature: malformedShader
level: shader
shader: features/not-a-map
parameters:
  color:
    kind: vec3
    value: [1.0, 1.0, 1.0]
    binding: FeatureUBO
    member: color
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "render feature with malformed shader should fail");
  EXPECT(hasDiagnosticContaining(parsed, "shader"),
         "diagnostic should name malformed shader");
}

void testRenderFeatureRejectsAbiParameterWithoutShaderUri() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-abi-no-shader", R"(
schema: lxe.render-feature.v1
name: AbiNoShader
feature: abiNoShader
level: shader
parameters:
  color:
    kind: vec3
    value: [1.0, 1.0, 1.0]
    binding: FeatureUBO
    member: color
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "ABI-owning parameter without shader URI should fail");
  EXPECT(hasDiagnosticContaining(parsed, "shader.uri"),
         "diagnostic should name missing shader URI");
}

void testRenderFeatureRejectsPassBindingMemberAndSpecialization() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-pass-binding", R"(
schema: lxe.render-feature.v1
name: PassFeature
feature: passFeature
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  enabled:
    kind: bool
    value: true
    binding: FeatureUBO
    member: enabled
  other:
    kind: bool
    value: false
    specialization:
      constantId: 5
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "pass-level feature binding/member/specialization should fail");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.enabled.binding"),
         "diagnostic should reject pass-level binding");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.enabled.member"),
         "diagnostic should reject pass-level member");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.other.specialization"),
         "diagnostic should reject parameter-level specialization");
}

void testRenderFeatureRejectsVolatileValueAndSpecializationFields() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto withValue = parser.parse("memory://feature-volatile-value", R"(
schema: lxe.render-feature.v1
name: PassFeature
feature: passFeature
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  enableIblLighting:
    kind: bool
    volatile: true
    value: true
    binding: PassRuntimeUBO
    member: enableIblLighting
)");

  EXPECT(!withValue.renderFeature.has_value(),
         "volatile pass parameter with value should fail");
  EXPECT(hasDiagnosticContaining(withValue,
                                 "volatile parameter must not define value"),
         "diagnostic should reject volatile value");

  const auto withConstantId =
      parser.parse("memory://feature-volatile-constant-id", R"(
schema: lxe.render-feature.v1
name: PassFeature
feature: passFeature
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  enableIblLighting:
    kind: bool
    volatile: true
    constantId: 7
    binding: PassRuntimeUBO
    member: enableIblLighting
)");

  EXPECT(!withConstantId.renderFeature.has_value(),
         "volatile pass parameter with constantId should fail");
  EXPECT(hasDiagnosticContaining(
             withConstantId, "volatile parameter must not define constantId"),
         "diagnostic should reject volatile constantId");

  const auto withSpecialization =
      parser.parse("memory://feature-volatile-specialization", R"(
schema: lxe.render-feature.v1
name: PassFeature
feature: passFeature
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  enableIblLighting:
    kind: bool
    volatile: true
    specialization:
      constantId: 7
    binding: PassRuntimeUBO
    member: enableIblLighting
)");

  EXPECT(!withSpecialization.renderFeature.has_value(),
         "volatile pass parameter with specialization metadata should fail");
  EXPECT(hasDiagnosticContaining(withSpecialization,
                                 "volatile parameter must not define "
                                 "specialization"),
         "diagnostic should reject volatile specialization metadata");
}

void testRenderFeatureAcceptsVolatilePassUniformField() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-volatile-uniform", R"(
schema: lxe.render-feature.v1
name: PassFeature
feature: passFeature
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  enableIblLighting:
    kind: bool
    volatile: true
    binding: PassRuntimeUBO
    member: enableIblLighting
    required: true
)");

  EXPECT(parsed.renderFeature.has_value(),
         "volatile pass uniform field should parse");
  EXPECT(parsed.diagnostics.empty(),
         "volatile pass uniform field should not emit diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }
  const auto parameter =
      parsed.renderFeature->parameters.find("enableIblLighting");
  EXPECT(parameter != parsed.renderFeature->parameters.end(),
         "volatile pass uniform field should be retained");
  if (parameter != parsed.renderFeature->parameters.end()) {
    EXPECT(parameter->second.binding == "PassRuntimeUBO",
           "volatile pass uniform field should retain binding");
    EXPECT(parameter->second.member == "enableIblLighting",
           "volatile pass uniform field should retain member");
    EXPECT(parameter->second.volatileRuntime,
           "volatile YAML field should be consumed into volatileRuntime");
  }
}

void testRenderFeatureRejectsMissingRequiredValue() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed =
      parser.parse("memory://surface-lighting-missing-value", R"(
schema: lxe.render-feature.v1
name: SurfaceLighting
feature: surfaceLighting
level: shader
shader:
  uri: features/surface_lighting
parameters:
  diffuseIblIntensity:
    kind: float
    binding: SurfaceLightingUBO
    member: diffuseIblIntensity
    required: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "required scalar parameter without value should fail");
  EXPECT(
      hasDiagnosticContaining(parsed, "parameters.diffuseIblIntensity.value"),
      "diagnostic should name missing required value");
}

void testRenderFeatureRejectsWrongBoolAndNumericValueTypes() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto wrongBool = parser.parse("memory://surface-lighting-bool-type", R"(
schema: lxe.render-feature.v1
name: SurfaceLighting
feature: surfaceLighting
level: shader
shader:
  uri: features/surface_lighting
parameters:
  enableIblLighting:
    kind: bool
    value: yes
    binding: SurfaceLightingUBO
    member: enableIblLighting
    required: true
)");

  EXPECT(!wrongBool.renderFeature.has_value(),
         "bool parameter with non-bool value should fail");
  EXPECT(
      hasDiagnosticContaining(wrongBool, "parameters.enableIblLighting.value"),
      "diagnostic should name wrong bool value");

  const auto wrongFloat =
      parser.parse("memory://surface-lighting-float-type", R"(
schema: lxe.render-feature.v1
name: SurfaceLighting
feature: surfaceLighting
level: shader
shader:
  uri: features/surface_lighting
parameters:
  diffuseIblIntensity:
    kind: float
    value: bright
    binding: SurfaceLightingUBO
    member: diffuseIblIntensity
    required: true
)");

  EXPECT(!wrongFloat.renderFeature.has_value(),
         "float parameter with non-numeric value should fail");
  EXPECT(hasDiagnosticContaining(wrongFloat,
                                 "parameters.diffuseIblIntensity.value"),
         "diagnostic should name wrong numeric value");
}

void testRenderFeatureRequiredBufferUsesUriNotValue() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-required-buffer", R"(
schema: lxe.render-feature.v1
name: BufferFeature
feature: bufferFeature
level: shader
shader:
  uri: features/buffer_feature
parameters:
  sampleBuffer:
    kind: buffer
    uri: assets/buffers/sample.bin
    binding: SampleBuffer
    required: true
)");

  EXPECT(parsed.renderFeature.has_value(),
         "required buffer parameter with uri should parse without value");
  EXPECT(parsed.diagnostics.empty(),
         "required buffer parameter with uri should not emit diagnostics");
  if (parsed.renderFeature.has_value()) {
    const auto &parameter = parsed.renderFeature->parameters.at("sampleBuffer");
    EXPECT(parameter.uri == LX_core::ResourceUri("assets/buffers/sample.bin"),
           "required buffer uri should be retained");
    EXPECT(parameter.value.empty(),
           "required buffer parameter should not need scalar value");
  }

  const auto missingUri =
      parser.parse("memory://feature-required-buffer-missing-uri", R"(
schema: lxe.render-feature.v1
name: BufferFeature
feature: bufferFeature
level: shader
shader:
  uri: features/buffer_feature
parameters:
  sampleBuffer:
    kind: buffer
    binding: SampleBuffer
    required: true
)");

  EXPECT(!missingUri.renderFeature.has_value(),
         "required buffer parameter without uri should fail");
  EXPECT(hasDiagnosticContaining(missingUri, "parameters.sampleBuffer.uri"),
         "diagnostic should name missing buffer uri");
}

void testTextureResourceParserUsesDeclaredContentFormat() {
  LX_infra::TextureResourceParser parser;
  LX_core::SceneResourceTable colorTable;
  const LX_core::ResourceUri colorUri(
      "assets://models/damaged_helmet/Default_albedo.jpg");
  const auto parsedColor =
      parser.parse(colorTable, colorUri,
                   LX_infra::SceneResourceParseContext{
                       .textureContent = LX_core::TextureContent::Color,
                   });
  EXPECT(parsedColor.diagnostics.empty(),
         "color texture parse should not emit diagnostics");
  const auto colorHandle = colorTable.findTexture(colorUri);
  EXPECT(colorHandle.has_value(), "color texture should register");
  if (colorHandle.has_value()) {
    const auto colorTexture = colorTable.resolve(*colorHandle);
    EXPECT(colorTexture.has_value(), "color texture handle should resolve");
    if (colorTexture.has_value()) {
      EXPECT(colorTexture->get().texture()->desc().format ==
                 LX_core::TextureFormat::RGBA8Srgb,
             "declared color texture should upload as sRGB");
    }
  }

  LX_core::SceneResourceTable normalTable;
  const LX_core::ResourceUri normalUri(
      "assets://models/damaged_helmet/Default_normal.jpg");
  const auto parsedNormal =
      parser.parse(normalTable, normalUri,
                   LX_infra::SceneResourceParseContext{
                       .textureContent = LX_core::TextureContent::Normal,
                   });
  EXPECT(parsedNormal.diagnostics.empty(),
         "normal texture parse should not emit diagnostics");
  const auto normalHandle = normalTable.findTexture(normalUri);
  EXPECT(normalHandle.has_value(), "normal texture should register");
  if (normalHandle.has_value()) {
    const auto normalTexture = normalTable.resolve(*normalHandle);
    EXPECT(normalTexture.has_value(), "normal texture handle should resolve");
    if (normalTexture.has_value()) {
      EXPECT(normalTexture->get().texture()->desc().format ==
                 LX_core::TextureFormat::RGBA8,
             "declared normal texture should remain linear RGBA8");
    }
  }
}

void testMaterialParserAnnotatesTextureDependencyContent() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const LX_core::ResourceUri materialUri(
      "assets/scenes/generated/materials/damaged_helmet_standard_pbr.material");
  const auto parsed =
      parser.parse(table, materialUri, readTextFile(materialUri.string()));
  EXPECT(parsed.diagnostics.empty(),
         "helmet standard-pbr material should parse cleanly");
  EXPECT(!parsed.dependencies.empty(),
         "helmet material should declare texture dependencies");

  auto contentFor = [&](const std::string &parameterName) {
    for (const auto &dependency : parsed.dependencies) {
      if (dependency.parameterName == parameterName) {
        return dependency.textureContent;
      }
    }
    return LX_core::TextureContent::Unknown;
  };

  EXPECT(contentFor("baseColorTexture") == LX_core::TextureContent::Color,
         "baseColorTexture dependency should be color/sRGB");
  EXPECT(contentFor("emissiveTexture") == LX_core::TextureContent::Color,
         "emissiveTexture dependency should be color/sRGB");
  EXPECT(contentFor("metallicRoughnessTexture") ==
             LX_core::TextureContent::MetallicRoughness,
         "metallicRoughnessTexture dependency should be linear data");
  EXPECT(contentFor("normalTexture") == LX_core::TextureContent::Normal,
         "normalTexture dependency should be normal data");
  EXPECT(contentFor("occlusionTexture") == LX_core::TextureContent::Occlusion,
         "occlusionTexture dependency should be occlusion data");
}

// REQ-073-e2 Task 2 keeps the default render-path assets as positive coverage
// for the target input-contract schema.
void testDefaultRenderPathGraphAssetParses() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse(
      "assets/render_paths/forward_main.render-path.yaml",
      readTextFile("assets/render_paths/forward_main.render-path.yaml"));

  EXPECT(parsed.renderPathGraph.has_value(),
         "default forward render path graph asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "default forward render path graph asset should not emit diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }

  const auto &graph = *parsed.renderPathGraph;
  EXPECT(graph.name == "ForwardMain",
         "default forward graph should retain name");
  EXPECT(graph.renderPath == LX_core::RenderPath::Forward,
         "default forward graph should use Forward render path");
  EXPECT(graph.features.size() == 6,
         "default forward graph should reference all forward feature assets");
  for (const char *slot : {"forwardPass", "skybox", "environmentLighting",
                           "surfaceLighting", "toneMapping", "bloom"}) {
    EXPECT(renderPathGraphHasFeature(graph, slot),
           std::string("default forward graph should reference feature.") +
               slot);
  }
  EXPECT(!renderPathGraphHasPass(graph, "ForwardIblLighting"),
         "default forward graph should keep IBL inline instead of adding a "
         "ForwardIblLighting pass");
  EXPECT(graph.passes.size() == 4,
         "default forward graph should declare Shadow, Forward, Bloom, and "
         "DebugOverlay passes");
  if (graph.passes.size() == 4) {
    EXPECT(graph.passes[1].id == "Forward",
           "default forward graph should keep background and opaque draws in "
           "the Forward pass");
    EXPECT(std::find(graph.passes[1].input.material.types.begin(),
                     graph.passes[1].input.material.types.end(),
                     "unlit-texture") !=
               graph.passes[1].input.material.types.end(),
           "default Forward pass should accept generated unlit texture room "
           "draws");
    for (const char *source : {"feature.forwardPass", "feature.skybox",
                               "feature.environmentLighting",
                               "feature.surfaceLighting",
                               "scene.environmentBake",
                               "scene.materialIblBake",
                               "feature.toneMapping"}) {
      EXPECT(renderPassHasSource(graph.passes[1], source),
             std::string("default Forward pass should consume ") + source);
    }
    EXPECT(std::find(graph.passes[1].targets.begin(),
                     graph.passes[1].targets.end(),
                     "hdr.color") != graph.passes[1].targets.end(),
           "default Forward pass should write HDR color for post passes");
    EXPECT(graph.passes[2].id == "Bloom",
           "default forward graph should include Bloom pass after Forward");
    EXPECT(std::find(graph.passes[2].sources.begin(),
                     graph.passes[2].sources.end(),
                     "hdr.color") != graph.passes[2].sources.end(),
           "Bloom pass should read Forward color output");
    EXPECT(std::find(graph.passes[2].sources.begin(),
                     graph.passes[2].sources.end(),
                     "feature.bloom") != graph.passes[2].sources.end(),
           "Bloom pass should consume feature.bloom");
    EXPECT(std::find(graph.passes[2].targets.begin(),
                     graph.passes[2].targets.end(),
                     "swapchain.color") != graph.passes[2].targets.end(),
           "Bloom pass should write final swapchain color");
  }
  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "default forward graph asset should compile into a FrameGraph plan");
  if (compiled.getPasses().size() == 4) {
    EXPECT(compiled.getPasses()[0].name == LX_core::StringID("Shadow"),
           "default forward graph should run shadow pass first");
    EXPECT(compiled.getPasses()[1].name == LX_core::StringID("Forward"),
           "default forward graph should run all scene draws after shadow");
    EXPECT(compiled.getPasses()[2].name == LX_core::StringID("Bloom"),
           "default forward graph should run Bloom after Forward");
    EXPECT(compiled.getPasses()[3].name == LX_core::StringID("DebugOverlay"),
           "default forward graph should run debug overlay last");
  }
}

void testDefaultDeferredRenderPathGraphAssetParses() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse(
      "assets/render_paths/deferred_main.render-path.yaml",
      readTextFile("assets/render_paths/deferred_main.render-path.yaml"));

  EXPECT(parsed.renderPathGraph.has_value(),
         "default deferred render path graph asset should parse");
  EXPECT(
      parsed.diagnostics.empty(),
      "default deferred render path graph asset should not emit diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }

  const auto &graph = *parsed.renderPathGraph;
  EXPECT(graph.name == "DeferredMain",
         "default deferred graph should retain name");
  EXPECT(graph.renderPath == LX_core::RenderPath::Deferred,
         "default deferred graph should use Deferred render path");
  EXPECT(renderPathGraphHasFeature(graph, "surfaceLighting"),
         "default deferred graph should reference feature.surfaceLighting");
  EXPECT(!renderPathGraphHasPass(graph, "ForwardIblLighting"),
         "default deferred graph should not add a ForwardIblLighting pass");
  EXPECT(graph.passes.size() == 4,
         "default deferred graph should declare shadow, GBuffer, lighting, "
         "and debug overlay passes");
  if (graph.passes.size() == 4) {
    EXPECT(std::find(graph.passes[1].input.material.types.begin(),
                     graph.passes[1].input.material.types.end(),
                     "environment-box") ==
               graph.passes[1].input.material.types.end(),
           "default deferred GBuffer pass should not accept environment-box "
           "until deferred background participates in the lighting pass");
    for (const char *source : {"feature.toneMapping",
                               "feature.surfaceLighting",
                               "scene.environmentBake",
                               "scene.materialIblBake"}) {
      EXPECT(renderPassHasSource(graph.passes[2], source),
             std::string("default DeferredLighting pass should consume ") +
                 source);
    }
    EXPECT(std::find(graph.passes[2].targets.begin(),
                     graph.passes[2].targets.end(),
                     "swapchain.color") != graph.passes[2].targets.end(),
           "default deferred lighting pass should write final swapchain color");
  }
  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "default deferred graph asset should compile into a FrameGraph plan");
  if (compiled.getPasses().size() == 4) {
    EXPECT(compiled.getPasses()[0].name == LX_core::StringID("Shadow"),
           "default deferred graph should run shadow pass first");
    EXPECT(compiled.getPasses()[1].name == LX_core::StringID("Deferred"),
           "default deferred graph should run GBuffer producer after shadow");
    EXPECT(compiled.getPasses()[2].name ==
               LX_core::StringID("DeferredLighting"),
           "default deferred graph should run lighting after GBuffer");
    EXPECT(compiled.getPasses()[3].name == LX_core::StringID("DebugOverlay"),
           "default deferred graph should run debug overlay last");
  }
}

void testSurfaceLightingRenderPathAssetsDeclareBakeFacts() {
  struct ExpectedPass final {
    const char *path;
    const char *featurePass;
  };
  const ExpectedPass assets[] = {
      {"assets/render_paths/forward_main.render-path.yaml", "Forward"},
      {"assets/render_paths/deferred_main.render-path.yaml",
       "DeferredLighting"},
      {"assets/render_paths/deferred_bloom.render-path.yaml",
       "DeferredLighting"},
      {"assets/render_paths/debug_color_transfer.render-path.yaml",
       "Forward"},
  };

  LX_infra::RenderPathGraphResourceParser parser;
  for (const ExpectedPass &asset : assets) {
    const auto parsed = parser.parse(asset.path, readTextFile(asset.path));
    EXPECT(parsed.renderPathGraph.has_value(),
           std::string(asset.path) + " should parse");
    EXPECT(parsed.diagnostics.empty(),
           std::string(asset.path) + " should not emit diagnostics");
    if (!parsed.renderPathGraph.has_value()) {
      continue;
    }

    const auto &graph = *parsed.renderPathGraph;
    EXPECT(renderPathGraphHasFeature(graph, "surfaceLighting"),
           std::string(asset.path) + " should declare feature.surfaceLighting");
    EXPECT(!renderPathGraphHasPass(graph, "ForwardIblLighting"),
           std::string(asset.path) +
               " should not add a separate ForwardIblLighting pass");

    const auto pass = std::find_if(
        graph.passes.begin(), graph.passes.end(),
        [&](const LX_core::RenderPassNode &candidate) {
          return candidate.id == asset.featurePass;
        });
    if (pass == graph.passes.end()) {
      EXPECT(false, std::string(asset.path) + " should contain pass " +
                        asset.featurePass);
      continue;
    }
    for (const char *source : {"feature.surfaceLighting",
                               "scene.environmentBake",
                               "scene.materialIblBake"}) {
      EXPECT(renderPassHasSource(*pass, source),
             std::string(asset.path) + " " + asset.featurePass +
                 " should consume " + source);
    }
  }
}

void testRenderPathGraphAcceptsSurfaceLightingFeatureSources() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://surface-lighting-shared-graph", R"(
schema: lxe.render-path-graph.v1
name: SurfaceLightingSharedGraph
renderPath: Forward
features:
  surfaceLighting:
    uri: assets/effects/surface_lighting.render-feature.yaml
passes:
  - id: Forward
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Forward/pbr
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [feature.surfaceLighting, scene.environmentBake, scene.materialIblBake]
    targets: [hdr.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
  - id: DeferredLighting
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Deferred/deferred_lighting
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: swapchain.color
          format: BGRA8Srgb
          samples: 1
          layers: 1
    sources: [hdr.color, feature.surfaceLighting, scene.environmentBake, scene.materialIblBake]
    targets: [swapchain.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  EXPECT(parsed.renderPathGraph.has_value(),
         "render path graph should accept surfaceLighting feature sources");
  EXPECT(parsed.diagnostics.empty(),
         "surfaceLighting and IBL bake graph sources should not emit unknown "
         "resource diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }
  const auto &graph = *parsed.renderPathGraph;
  EXPECT(graph.features.size() == 1 &&
             graph.features.front().slot == "surfaceLighting",
         "graph should retain one surfaceLighting feature dependency");
  EXPECT(graph.passes.size() == 2,
         "graph should retain Forward and DeferredLighting passes");
  if (graph.passes.size() == 2) {
    for (const auto &pass : graph.passes) {
      EXPECT(std::find(pass.sources.begin(), pass.sources.end(),
                       "feature.surfaceLighting") != pass.sources.end(),
             pass.id + " should consume feature.surfaceLighting");
      EXPECT(renderPassHasSource(pass, "scene.environmentBake"),
             pass.id + " should consume scene.environmentBake");
      EXPECT(renderPassHasSource(pass, "scene.materialIblBake"),
             pass.id + " should consume scene.materialIblBake");
    }
  }
  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "surfaceLighting IBL bake facts should compile through the default "
         "FrameGraph registry");
}

void testBakeRenderPathGraphAssetsParseAndCompile() {
  struct BakeAsset final {
    const char *path;
    const char *name;
    std::size_t passCount;
    std::size_t payloadCount;
  };
  const BakeAsset assets[] = {
      {"assets/render_paths/bake_environment_ibl.render-path.yaml",
       "EnvironmentIblBake", 3, 3},
      {"assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml",
       "StandardPbrBrdfLutBake", 1, 1},
  };

  LX_infra::RenderPathGraphResourceParser parser;
  for (const BakeAsset &asset : assets) {
    const auto parsed = parser.parse(asset.path, readTextFile(asset.path));
    if (!parsed.diagnostics.empty()) {
      for (const std::string &diagnostic : parsed.diagnostics) {
        std::cerr << "[diag] " << diagnostic << '\n';
      }
    }
    EXPECT(parsed.renderPathGraph.has_value(),
           std::string(asset.path) + " should parse");
    EXPECT(parsed.diagnostics.empty(),
           std::string(asset.path) + " should not emit diagnostics");
    if (!parsed.renderPathGraph.has_value()) {
      continue;
    }

    const auto &graph = *parsed.renderPathGraph;
    EXPECT(graph.name == asset.name,
           std::string(asset.path) + " should retain graph name");
    EXPECT(graph.renderPath == LX_core::RenderPath::OfflineRT,
           std::string(asset.path) + " should use OfflineRT domain");
    EXPECT(graph.passes.size() == asset.passCount,
           std::string(asset.path) + " should retain bake pass count");
    std::size_t payloadCount = 0;
    for (const LX_core::RenderPassNode &pass : graph.passes) {
      payloadCount += pass.payloads.size();
      EXPECT(!pass.payloads.empty(),
             std::string(asset.path) +
                 " every bake pass should declare payload outputs");
    }
    EXPECT(payloadCount == asset.payloadCount,
           std::string(asset.path) + " should retain payload count");

    const LX_core::FrameGraph frameGraph =
        LX_core::buildFrameGraphFromRenderPathGraph(
            graph, LX_core::GraphResourceRegistry::makeDefault());
    const auto compiled =
        frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
    EXPECT(compiled.isValid(),
           std::string(asset.path) +
               " should compile into a bake FrameGraph plan");
    if (compiled.isValid()) {
      std::size_t compiledPayloadCount = 0;
      for (const LX_core::CompiledFrameGraphPass &pass : compiled.getPasses()) {
        compiledPayloadCount += pass.payloads.size();
      }
      EXPECT(compiledPayloadCount == asset.payloadCount,
             std::string(asset.path) +
                 " compiled graph should retain payload contracts");
      if (std::string(asset.name) == "StandardPbrBrdfLutBake" &&
          !compiled.getPasses().empty()) {
        const auto colorFormats =
            compiled.getPasses().front().target.getColorFormats();
        EXPECT(!colorFormats.empty() &&
                   colorFormats.front() == LX_core::ImageFormat::RG16Float,
               "BRDF LUT compiled target should retain RG16Float format");
      }
    }
  }
}

void testBakeRenderPathGraphAssetsResolveShaderPayloads() {
  struct BakeAsset final {
    const char *path;
    std::size_t shaderCount;
  };
  const BakeAsset assets[] = {
      {"assets/render_paths/bake_environment_ibl.render-path.yaml", 3},
      {"assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml", 1},
  };

  for (const BakeAsset &asset : assets) {
    LX_infra::SceneResourceParserRegistry registry;
    LX_infra::registerRenderResourceParsers(registry);
    LX_core::SceneResourceTable table;

    const auto parsed =
        registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                       LX_core::ResourceUri(asset.path),
                       LX_infra::SceneResourceParseContext{});

    if (!parsed.diagnostics.empty()) {
      for (const std::string &diagnostic : parsed.diagnostics) {
        std::cerr << "[diag] " << diagnostic << '\n';
      }
    }
    EXPECT(parsed.diagnostics.empty(),
           std::string(asset.path) +
               " should resolve bake shader dependencies");
    EXPECT(parsed.identity.isValid(),
           std::string(asset.path) + " should register render path graph");
    EXPECT(table.shaderCount() == asset.shaderCount,
           std::string(asset.path) +
               " should register one typed shader per bake pass");
  }
}

void testRenderFeatureRejectsRenderFlowFields() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://bad-feature", R"(
schema: lxe.render-feature.v1
name: BadFeature
feature: toneMapping
level: shader
shader:
  uri: features/tone_mapping
pass: ToneMap
passes:
  - id: ToneMap
renderState:
  cullMode: None
parameters:
  exposure:
    kind: float
    value: 1.0
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "render feature with render-flow fields should fail");
  EXPECT(hasDiagnosticContaining(parsed, "pass"),
         "diagnostic should reject legacy singular pass");
  EXPECT(hasDiagnosticContaining(parsed, "passes"),
         "diagnostic should reject passes");
  EXPECT(hasDiagnosticContaining(parsed, "renderState"),
         "diagnostic should reject renderState");
}

void testRenderFeatureResourcesAreExplicitlyNotImplemented() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-with-resources", R"(
schema: lxe.render-feature.v1
name: FeatureWithResources
feature: toneMapping
level: shader
shader:
  uri: features/tone_mapping
resources:
  exposureLut:
    uri: textures/exposure_lut.ktx
parameters:
  exposure:
    kind: float
    value: 1.0
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "render feature resources must not be silently ignored");
  EXPECT(hasDiagnosticContaining(parsed, "resources"),
         "diagnostic should include resources field");
  EXPECT(hasDiagnosticContaining(parsed, "resources not implemented"),
         "diagnostic should state resources are not implemented");
}

void testRenderFeatureRejectsUnknownParameterFields() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://feature-parameter-extra", R"(
schema: lxe.render-feature.v1
name: FeatureWithParameterExtra
feature: toneMapping
level: shader
shader:
  uri: features/tone_mapping
parameters:
  exposure:
    kind: float
    value: 1.0
    ignoredField: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "render feature parameter fields must not be silently ignored");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.exposure.ignoredField"),
         "diagnostic should reject unknown parameter field");
}

void testRenderFeatureRejectsUnknownParameterField() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed =
      parser.parse("memory://feature-parameter-extra-singular", R"(
schema: lxe.render-feature.v1
name: FeatureWithParameterExtra
feature: toneMapping
level: shader
shader:
  uri: features/tone_mapping
parameters:
  exposure:
    kind: float
    value: 1.0
    skyboxEnabled: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "singular unknown parameter field test should fail");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.exposure.skyboxEnabled"),
         "diagnostic should reject skyboxEnabled parameter field");
}

void testEnvironmentLightingFeatureRejectsMissingEnvironmentMapUri() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-feature-missing-uri",
                                   R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    binding: SkyboxMap
    required: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "environmentLighting without environmentMap.uri should fail");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.environmentMap.uri"),
         "diagnostic should name parameters.environmentMap.uri");
}

void testRenderPathFeatureValidationRejectsManualGammaOnSrgbForwardTarget() {
  LX_core::RenderPathGraph graph;
  graph.name = "InvalidForwardSrgbGamma";
  graph.renderPath = LX_core::RenderPath::Forward;

  LX_core::RenderPassNode forwardPass;
  forwardPass.id = "Forward";
  forwardPass.shaderUri = LX_core::ResourceUri("render_paths/Forward/pbr");
  forwardPass.input.kind = LX_core::RenderPassInputKind::SceneRenderables;
  forwardPass.input.geometry = LX_core::RenderPathGeometryContract{
      .vertex = LX_core::RenderPathGeometryVertexContract::PositionOnly,
      .topology = LX_core::PrimitiveTopology::TriangleList,
  };
  forwardPass.attachments.push_back(LX_core::RenderPathAttachmentContract{
      .target = "swapchain.color",
      .format = LX_core::ImageFormat::BGRA8Srgb,
      .samples = 1,
      .layers = 1,
      .depth = false,
      .attachmentUsage =
          LX_core::RenderPathAttachmentUsage::ColorAttachmentWrite,
  });
  forwardPass.sources = {"scene.camera", "feature.forwardPass"};
  forwardPass.targets = {"swapchain.color"};
  graph.passes.push_back(std::move(forwardPass));

  LX_core::FrameGraph frameGraph;
  LX_core::FramePass compiledForward;
  compiledForward.name = LX_core::StringID("Forward");
  compiledForward.writes.push_back(LX_core::FrameGraphWrite{
      LX_core::FrameGraphResourceRef::colorAttachment(
          LX_core::StringID("swapchain.color")),
      std::nullopt,
  });
  frameGraph.addPass(std::move(compiledForward));

  LX_core::SceneResourceTable resources;
  LX_core::RenderFeature forwardFeature;
  forwardFeature.name = "ForwardPass";
  forwardFeature.feature = "forwardPass";
  forwardFeature.level = LX_core::RenderFeatureLevel::Pass;
  forwardFeature.parameters["enable_gamma"] = LX_core::RenderFeatureParameter{
      .kind = "bool",
      .value = "true",
      .required = true,
  };
  const LX_core::RenderFeatureHandle featureHandle =
      resources.registerRenderFeature(
          LX_core::ResourceUri(
              "memory://effects/forward_pass.render-feature.yaml"),
          std::move(forwardFeature));
  EXPECT(featureHandle.isValid(),
         "test forwardPass feature should register before validation");

  const auto diagnostics = LX_core::validateRenderPathFeatureCombination(
      graph, frameGraph, resources);

  EXPECT(!diagnostics.empty(),
         "manual gamma on sRGB Forward target should emit diagnostics");
  const auto fatal = std::find_if(
      diagnostics.begin(), diagnostics.end(),
      [](const LX_core::RenderPathFeatureValidationDiagnostic &diagnostic) {
        return diagnostic.fatal &&
               diagnostic.message.find(
                   "FATAL: sRGB target must not use manual gamma") !=
                   std::string::npos;
      });
  EXPECT(fatal != diagnostics.end(),
         "diagnostic should reject sRGB target plus manual gamma");
}

void testParserAdapterRejectsManualGammaOnSrgbForwardTarget() {
  const LX_core::ResourceUri featureUri = writeTempRenderPathGraph(
      "lxe_invalid_forward_pass.render-feature.yaml", R"(
schema: lxe.render-feature.v1
name: ForwardPass
feature: forwardPass
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  enable_gamma:
    kind: bool
    value: true
    required: true
)");
  const std::filesystem::path featurePath = std::filesystem::path(
      featureUri.string().substr(std::string("file://").size()));

  const LX_core::ResourceUri graphUri = writeTempRenderPathGraph(
      "lxe_invalid_forward_srgb_gamma.render-path.yaml",
      "schema: lxe.render-path-graph.v1\n"
      "name: InvalidForwardSrgbGamma\n"
      "renderPath: Forward\n"
      "features:\n"
      "  forwardPass:\n"
      "    uri: " +
          featurePath.filename().generic_string() +
          "\n"
          "passes:\n"
          "  - id: Forward\n"
          "    stage: raster\n"
          "    dispatch: draw\n"
          "    shader: render_paths/Debug/debug_overlay\n"
          "    input:\n"
          "      kind: scene-renderables\n"
          "      geometry:\n"
          "        vertex: position-only\n"
          "        topology: triangle-list\n"
          "    rendering:\n"
          "      mode: dynamic\n"
          "      attachments:\n"
          "        - target: swapchain.color\n"
          "          format: BGRA8Srgb\n"
          "          samples: 1\n"
          "          layers: 1\n"
          "    sources: [scene.camera, feature.forwardPass]\n"
          "    targets: [swapchain.color]\n"
          "    renderState:\n"
          "      cullMode: Back\n"
          "      depthTest: true\n"
          "      depthWrite: true\n"
          "      depthOp: LessEqual\n");

  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const auto parsed =
      registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                     graphUri, LX_infra::SceneResourceParseContext{});

  EXPECT(!parsed.identity.isValid() ||
             parsed.metadata.state == LX_core::ResourceState::Failed,
         "invalid feature/target combination should fail graph load");
  EXPECT(hasDiagnosticContaining(
             parsed, "FATAL: sRGB target must not use manual gamma"),
         "graph load diagnostic should include fatal feature validation");
  EXPECT(table.renderPathGraphCount() == 0,
         "fatal feature validation must not register a half-loaded graph");
}

void testRenderPathGraphRejectsEmptyPassContracts() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://bad-render-path", R"(
schema: lxe.render-path-graph.v1
name: BadForward
renderPath: Forward
passes:
  - id: EmptyShader
    stage: raster
    dispatch: draw
    shader: ""
    input:
      kind: scene-renderables
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: EmptySources
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/pbr
    input:
      kind: scene-renderables
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: []
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: EmptyTargets
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/pbr
    input:
      kind: scene-renderables
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [scene.camera]
    targets: []
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "render path graph with empty pass contracts should fail");
  EXPECT(hasDiagnosticContaining(parsed, "passes.EmptyShader.shader"),
         "diagnostic should include empty shader field");
  EXPECT(hasDiagnosticContaining(parsed, "passes.EmptySources.sources"),
         "diagnostic should include empty sources field");
  EXPECT(hasDiagnosticContaining(parsed, "passes.EmptyTargets.targets"),
         "diagnostic should include empty targets field");
}

void testBakeRenderPathGraphRejectsMissingPayloadDeclaration() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://bake-missing-payload", R"(
schema: lxe.render-path-graph.v1
name: BakeMissingPayload
renderPath: OfflineRT
passes:
  - id: BakeEnvironmentDiffuse
    stage: compute
    dispatch: compute
    shader: render_paths/Bake/environment_diffuse_sh9
    input:
      kind: compute-dispatch
    sources: [bake.environment.cubemap]
    targets: [bake.environment.diffuse_sh9]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "bake graph without payload declaration should fail");
  EXPECT(hasDiagnosticContaining(parsed, "payload"),
         "diagnostic should name missing bake payload declaration");
}

void testBakeRenderPathGraphRejectsPayloadMissingFormat() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://bake-payload-missing-format", R"(
schema: lxe.render-path-graph.v1
name: BakePayloadMissingFormat
renderPath: OfflineRT
passes:
  - id: BakeEnvironmentDiffuse
    stage: compute
    dispatch: compute
    shader: render_paths/Bake/environment_diffuse_sh9
    input:
      kind: compute-dispatch
    sources: [bake.environment.cubemap]
    targets: [bake.environment.diffuse_sh9]
    payloads:
      - name: diffuse_sh9
        target: bake.environment.diffuse_sh9
        kind: sh9
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "bake payload without format should fail");
  EXPECT(hasDiagnosticContaining(parsed, "payloads[0].format"),
         "diagnostic should name missing bake payload format");
}

void testBakeRenderPathGraphRejectsPayloadWithoutTarget() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://bake-payload-without-target", R"(
schema: lxe.render-path-graph.v1
name: BakePayloadWithoutTarget
renderPath: OfflineRT
passes:
  - id: BakeEnvironmentDiffuse
    stage: compute
    dispatch: compute
    shader: render_paths/Bake/environment_diffuse_sh9
    input:
      kind: compute-dispatch
    sources: [bake.environment.cubemap]
    targets: [bake.environment.diffuse_sh9]
    payloads:
      - name: diffuse_sh9
        format: SH9RgbFloat
        kind: sh9
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "bake payload without target should fail");
  EXPECT(hasDiagnosticContaining(parsed, "payloads[0].target"),
         "diagnostic should name missing bake payload target");
}

void testBakeRenderPathGraphRejectsPayloadTargetFormatMismatch() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://bake-payload-format-mismatch", R"(
schema: lxe.render-path-graph.v1
name: BakePayloadFormatMismatch
renderPath: OfflineRT
passes:
  - id: BakeMaterialBrdf
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Bake/standard_pbr_brdf_lut
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: bake.material.brdf_lut
          format: RG16Float
          samples: 1
          layers: 1
    sources: [bake.material.source]
    targets: [bake.material.brdf_lut]
    payloads:
      - name: brdf_lut
        target: bake.material.brdf_lut
        format: SH9RgbFloat
        kind: sh9
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "bake payload target with wrong format/kind should fail");
  EXPECT(hasDiagnosticContaining(parsed, "payloads[0]"),
         "diagnostic should name mismatched bake payload");
}

void testRenderPathGraphRejectsUnparsedAllowedLookingFields() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://ignored-fields", R"(
schema: lxe.render-path-graph.v1
name: IgnoredFields
renderPath: Forward
unknownRoot: true
features:
  toneMapping:
    uri: effects/tone_mapping.render-feature.yaml
    fallback: silent
passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/pbr
    input:
      kind: scene-renderables
      object:
        renderClass: [surface.opaque]
        ignoredFilter: true
      material:
        type: [matte]
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [geometry.vertex, scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "render path graph parser must fail unparsed fields");
  EXPECT(hasDiagnosticContaining(parsed, "unknownRoot"),
         "diagnostic should reject unknown root field");
  EXPECT(hasDiagnosticContaining(parsed, "features.toneMapping.fallback"),
         "diagnostic should reject unknown feature dependency field");
  EXPECT(hasDiagnosticContaining(parsed,
                                 "passes.Forward.input.object.ignoredFilter"),
         "diagnostic should reject unknown object input filter field");
}

void testLegacyRenderEffectSchemaIsRejectedByNewParser() {
  LX_infra::RenderPathGraphResourceParser graphParser;
  const auto parsedGraph = graphParser.parse("memory://legacy-effect", R"(
schema: lxe.render-effect.v1
name: legacy
renderPaths:
  Forward:
    phase: post
    passes:
      Composite:
        shader: shaders/composite.effect
        stage: raster
        dispatch: fullscreen
        sources: [hdr.color]
        targets: [swapchain.color]
        renderState:
          cullMode: None
          depthTest: false
          depthWrite: false
          depthOp: LessEqual
)");

  EXPECT(!parsedGraph.renderPathGraph.has_value(),
         "legacy effect should not produce a graph");
  EXPECT(hasDiagnosticContaining(parsedGraph, "lxe.render-path-graph.v1"),
         "diagnostic should point to new render path graph schema");
  LX_infra::RenderFeatureResourceParser featureParser;
  const auto parsedFeature = featureParser.parse("memory://legacy-effect", R"(
schema: lxe.render-effect.v1
name: legacy
feature: legacy
)");
  EXPECT(!parsedFeature.renderFeature.has_value(),
         "legacy effect should not produce a feature");
  EXPECT(hasDiagnosticContaining(parsedFeature, "lxe.render-feature.v1"),
         "diagnostic should point to new render feature schema");
}

void testParserAdapterRejectsMissingGraphShaderDependency() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri graphUri =
      writeTempRenderPathGraph("lxe_missing_shader.render-path.yaml", R"(
schema: lxe.render-path-graph.v1
name: MissingShader
renderPath: Forward
passes:
  - id: Broken
    stage: raster
    dispatch: draw
    shader: missing/not_real_shader
    input:
      kind: scene-renderables
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)");

  const auto parsed =
      registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                     graphUri, LX_infra::SceneResourceParseContext{});

  EXPECT(!parsed.identity.isValid() ||
             parsed.metadata.state == LX_core::ResourceState::Failed,
         "missing graph shader should fail graph parse");
  EXPECT(hasDiagnosticContaining(parsed, graphUri.string()),
         "diagnostic should name graph URI");
  EXPECT(hasDiagnosticContaining(parsed, "missing/not_real_shader"),
         "diagnostic should name shader URI");
  EXPECT(hasDiagnosticContaining(parsed, "RenderPathGraphResourceParser"),
         "diagnostic should name parser");
  EXPECT(table.shaderCount() == 0,
         "missing graph shader must not register a typed shader descriptor");

  const auto exported = table.exportResourceGraph();
  bool foundFailedShader = false;
  for (const auto &resource : exported.resources) {
    if (resource.type == LX_core::SceneResourceType::Shader &&
        resource.uri == LX_core::ResourceUri("missing/not_real_shader") &&
        resource.state == LX_core::ResourceState::Failed) {
      foundFailedShader = true;
    }
  }
  EXPECT(foundFailedShader,
         "failed shader metadata should be interned only for diagnostics");
}

void testParserAdapterRejectsRootUtilityShaderUri() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri graphUri = writeTempRenderPathGraph(
      "lxe_root_utility_shader_rejected.render-path.yaml", R"(
schema: lxe.render-path-graph.v1
name: RootUtilityRejected
renderPath: Forward
passes:
  - id: PostProcess
    stage: raster
    dispatch: fullscreen
    shader: post_process
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: swapchain.color
          format: BGRA8
          samples: 1
          layers: 1
    sources: [hdr.color]
    targets: [swapchain.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  const auto parsed =
      registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                     graphUri, LX_infra::SceneResourceParseContext{});

  EXPECT(!parsed.identity.isValid() ||
             parsed.metadata.state == LX_core::ResourceState::Failed,
         "root utility shader URI should fail graph parse");
  EXPECT(hasDiagnosticContaining(parsed, "post_process"),
         "diagnostic should name rejected root utility shader URI");
  EXPECT(hasDiagnosticContaining(parsed, "unsupported shader URI"),
         "diagnostic should explain unsupported shader URI");
  EXPECT(hasDiagnosticContaining(parsed, "render_paths/"),
         "diagnostic should point authors at render_paths namespace");
  EXPECT(table.shaderCount() == 0,
         "root utility shader URI must not register a typed shader descriptor");
}

void testParserAdapterRejectsDirectShaderSourceUri() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri graphUri = writeTempRenderPathGraph(
      "lxe_direct_shader_source_rejected.render-path.yaml", R"(
schema: lxe.render-path-graph.v1
name: DirectShaderSourceRejected
renderPath: Forward
passes:
  - id: PostProcess
    stage: raster
    dispatch: fullscreen
    shader: assets/shaders/glsl/render_paths/Post/post_process.frag
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: swapchain.color
          format: BGRA8
          samples: 1
          layers: 1
    sources: [hdr.color]
    targets: [swapchain.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  const auto parsed =
      registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                     graphUri, LX_infra::SceneResourceParseContext{});

  EXPECT(!parsed.identity.isValid() ||
             parsed.metadata.state == LX_core::ResourceState::Failed,
         "direct shader source URI should fail graph parse");
  EXPECT(hasDiagnosticContaining(
             parsed, "assets/shaders/glsl/render_paths/Post/post_process.frag"),
         "diagnostic should name rejected direct shader source URI");
  EXPECT(hasDiagnosticContaining(parsed, "unsupported shader URI"),
         "diagnostic should explain unsupported shader URI");
  EXPECT(hasDiagnosticContaining(parsed, "render_paths/"),
         "diagnostic should point authors at render_paths namespace");
  EXPECT(table.shaderCount() == 0,
         "direct shader source URI must not register a typed shader "
         "descriptor");
}

void testParserAdapterResolvesRenderPathShaderUriForms() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri graphUri =
      writeTempRenderPathGraph("lxe_shader_forms.render-path.yaml", R"(
schema: lxe.render-path-graph.v1
name: ShaderForms
renderPath: Forward
passes:
  - id: ForwardPbr
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/pbr
    input:
      kind: scene-renderables
      material:
        type: [matte, uber, metal, substrate, standard-pbr]
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: DeferredGBuffer
    stage: raster
    dispatch: draw
    shader: render_paths/Deferred/pbr_gbuffer
    input:
      kind: scene-renderables
      material:
        type: [matte, uber, metal, substrate, standard-pbr]
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: gbuffer.albedo
          format: RGBA8
          samples: 1
          layers: 1
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera]
    targets: [gbuffer.albedo]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: DeferredLighting
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Deferred/deferred_lighting
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [gbuffer.albedo]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: PostProcess
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Post/post_process
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: swapchain.color
          format: BGRA8
          samples: 1
          layers: 1
    sources: [hdr.color]
    targets: [swapchain.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: BloomThreshold
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Post/bloom_threshold
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: bloom.threshold
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [hdr.color]
    targets: [bloom.threshold]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: BloomBlurH
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Post/bloom_blur_h
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: bloom.blur_h
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [bloom.threshold]
    targets: [bloom.blur_h]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: BloomBlurV
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Post/bloom_blur_v
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: bloom.blur_v
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [bloom.blur_h]
    targets: [bloom.blur_v]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)");

  const auto parsed =
      registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                     graphUri, LX_infra::SceneResourceParseContext{});

  EXPECT(parsed.diagnostics.empty(),
         "RenderPath shader URI forms should resolve without diagnostics");
  EXPECT(parsed.identity.isValid(),
         "graph with RenderPath shader URI forms should register");
  EXPECT(table.shaderCount() == 7,
         "each RenderPath shader URI form should register a typed shader");

  bool rejectedUnresolvedVariant = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    const std::string message = error.what();
    rejectedUnresolvedVariant =
        message.find("requires material source variant resolution") !=
        std::string::npos;
  }
  EXPECT(rejectedUnresolvedVariant,
         "graph-only upload view should reject variant-only shaders before "
         "material source variant resolver runs");
}

void testParserAdapterLoadsEnvironmentFeatureTextureDependency() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri featureUri = writeTempRenderPathGraph(
      "lxe_environment_texture_dependency.render-feature.yaml", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    uri: assets/env/khronos/neutral/ggx/specular.ktx2
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [1.0, 1.0, 1.0]
    binding: EnvironmentLightingUBO
    member: color
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: EnvironmentLightingUBO
    member: intensity
    required: true
  rotation:
    kind: float
    value: 0.0
    binding: EnvironmentLightingUBO
    member: rotation
    required: true
)");

  const auto parsed =
      registry.parse(table, LX_core::SceneResourceType::RenderFeature,
                     featureUri, LX_infra::SceneResourceParseContext{});

  if (!parsed.diagnostics.empty()) {
    for (const std::string &diagnostic : parsed.diagnostics) {
      std::cerr << "[diag] " << diagnostic << '\n';
    }
  }
  EXPECT(parsed.diagnostics.empty(),
         "environment feature texture dependency should load without "
         "diagnostics");
  EXPECT(parsed.identity.isValid(),
         "environment feature with texture dependency should register");
  const auto texture = table.findTexture(
      LX_core::ResourceUri("assets/env/khronos/neutral/ggx/specular.ktx2"));
  EXPECT(texture.has_value(),
         "environmentMap texture URI should register a live texture");
  const auto resources = table.getEnvironmentLightingResources();
  bool hasSkyboxMap = false;
  for (const auto &resource : resources) {
    if (resource.getBindingName() == LX_core::StringID("SkyboxMap")) {
      hasSkyboxMap = true;
    }
  }
  EXPECT(hasSkyboxMap,
         "environment feature texture dependency should satisfy SkyboxMap");
}

void testDefaultRenderPathGraphAssetsResolveLiveShaderPayloads() {
  struct ExpectedAsset final {
    const char *path;
    std::size_t shaderCount;
  };
  const ExpectedAsset assets[] = {
      {"assets/render_paths/forward_main.render-path.yaml", 4},
      {"assets/render_paths/deferred_main.render-path.yaml", 4},
      {"assets/render_paths/deferred_bloom.render-path.yaml", 4},
  };

  for (const ExpectedAsset &asset : assets) {
    LX_infra::SceneResourceParserRegistry registry;
    LX_infra::registerRenderResourceParsers(registry);
    LX_core::SceneResourceTable table;

    const auto parsed =
        registry.parse(table, LX_core::SceneResourceType::RenderPathGraph,
                       LX_core::ResourceUri(asset.path),
                       LX_infra::SceneResourceParseContext{});

    EXPECT(parsed.diagnostics.empty(),
           std::string(asset.path) +
               " should resolve graph dependencies without diagnostics");
    EXPECT(parsed.identity.isValid(),
           std::string(asset.path) + " should register a graph resource");
    EXPECT(parsed.metadata.state != LX_core::ResourceState::Failed,
           std::string(asset.path) + " should not register as failed");
    EXPECT(table.shaderCount() == asset.shaderCount,
           std::string(asset.path) +
               " should register one shader descriptor per graph pass");

    bool rejectedUnresolvedVariant = false;
    try {
      (void)table.buildUploadView();
    } catch (const std::exception &error) {
      const std::string message = error.what();
      rejectedUnresolvedVariant =
          message.find("requires material source variant resolution") !=
          std::string::npos;
    }
    EXPECT(rejectedUnresolvedVariant,
           std::string(asset.path) +
               " graph-only upload view should stop until material source "
               "variant resolver produces final shader reflection");
  }
}

void testSceneDocumentParsesEnvironmentNodeAndBakeMarkers() {
  const auto parsed = parseSceneYaml("lxe_environment_node.scene.yaml", R"(
scene:
  name: EnvironmentNode
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: sky
      name: sky
      environment:
        feature:
          uri: assets/effects/environment_lighting.render-feature.yaml
        bake:
          enabled: true
    - nodeName: helmet
      name: helmet
      bake:
        ibl:
          enabled: true
)");

  EXPECT(parsed.rootNode().children.size() == 2,
         "scene should retain authored child nodes");
  if (parsed.rootNode().children.size() < 2) {
    return;
  }

  const auto &environmentNode = parsed.rootNode().children[0];
  const auto &objectNode = parsed.rootNode().children[1];
  EXPECT(environmentNode.environment.has_value(),
         "environment node should parse");
  EXPECT(environmentNode.environment->featureUri ==
             LX_core::ResourceUri(
                 "assets/effects/environment_lighting.render-feature.yaml"),
         "environment node should retain feature uri");
  EXPECT(environmentNode.environment->bake.enabled,
         "environment node should retain bake.enabled");
  EXPECT(objectNode.bake.ibl.has_value() && objectNode.bake.ibl->enabled,
         "object node should retain top-level bake.ibl.enabled");
}

void testOfflineSceneLoaderRegistersEnvironmentNodeFeatureFromFileUri() {
  const LX_core::ResourceUri featureUri = writeTempRenderPathGraph(
      "lxe_offline_environment_node_file_uri.render-feature.yaml", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    uri: assets/env/khronos/neutral/ggx/specular.ktx2
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [1.0, 1.0, 1.0]
    binding: EnvironmentLightingUBO
    member: color
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: EnvironmentLightingUBO
    member: intensity
    required: true
  rotation:
    kind: float
    value: 0.0
    binding: EnvironmentLightingUBO
    member: rotation
    required: true
)");

  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("OfflineEnvironmentNode");
  document.setGameplayCameraPath("/camera");
  auto &root = document.mutableRootNode();
  root.nodeName = "scene_root";
  root.name = "";

  LX_infra::scene_io::SceneNodeDocument environmentNode;
  environmentNode.nodeName = "environment";
  environmentNode.name = "environment";
  environmentNode.environment = LX_core::SceneEnvironmentNode{
      .featureUri = featureUri,
      .bake = LX_core::SceneIblBakeMarker{.enabled = true},
  };
  root.children.push_back(std::move(environmentNode));

  LX_infra::scene_io::SceneNodeDocument cameraNode;
  cameraNode.nodeName = "camera";
  cameraNode.name = "camera";
  cameraNode.camera = LX_infra::scene_io::CameraNodeState{};
  root.children.push_back(std::move(cameraNode));

  LX_infra::scene_io::SceneNodeDocument objectNode;
  objectNode.nodeName = "cube";
  objectNode.name = "cube";
  objectNode.meshUri = "builtin://lxe_editor/primitives/cube";
  objectNode.materialUri = "assets/materials/pbr.material";
  root.children.push_back(std::move(objectNode));

  LX_infra::scene_io::SceneNodeDocument lightNode;
  lightNode.nodeName = "sun";
  lightNode.name = "sun";
  lightNode.light = LX_infra::scene_io::LightNodeState{};
  root.children.push_back(std::move(lightNode));

  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver{std::filesystem::current_path()}};
  const auto loaded = loader.load(document, "");
  const auto state = loaded.table.environmentRuntimeState();
  EXPECT(state.has_value() && state->nodePresent,
         "offline loader should register environment node runtime state");
  if (!state.has_value()) {
    return;
  }
  EXPECT(state->bakeRequested,
         "offline loader should preserve environment bake request");
  const auto feature = loaded.table.resolve(state->feature);
  EXPECT(feature.has_value(),
         "offline loader environment state should point at live feature");
  if (feature.has_value()) {
    EXPECT(feature->get().feature == "environmentLighting",
           "offline loader should bind environmentLighting feature payload");
  }
  EXPECT(loaded.table.metadata(state->feature).uri == featureUri,
         "offline loader should bind the feature handle returned for the node "
         "file URI");
}

void testOfflineSceneLoaderResolvesCacheEnvironmentFeatureUri() {
  const std::filesystem::path cacheFeaturePath =
      std::filesystem::current_path() / ".asset_cache" /
      "test/offline_environment_cache.render-feature.yaml";
  writeTextFile(cacheFeaturePath, R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    uri: assets/env/khronos/neutral/ggx/specular.ktx2
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [1.0, 1.0, 1.0]
    binding: EnvironmentLightingUBO
    member: color
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: EnvironmentLightingUBO
    member: intensity
    required: true
  rotation:
    kind: float
    value: 0.0
    binding: EnvironmentLightingUBO
    member: rotation
    required: true
)");

  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("OfflineCacheEnvironmentNode");
  document.setGameplayCameraPath("/camera");
  auto &root = document.mutableRootNode();
  root.nodeName = "scene_root";
  root.name = "";

  LX_infra::scene_io::SceneNodeDocument environmentNode;
  environmentNode.nodeName = "environment";
  environmentNode.name = "environment";
  environmentNode.environment = LX_core::SceneEnvironmentNode{
      .featureUri = LX_core::ResourceUri(
          "cache://test/offline_environment_cache.render-feature.yaml"),
      .bake = LX_core::SceneIblBakeMarker{.enabled = false},
  };
  root.children.push_back(std::move(environmentNode));

  LX_infra::scene_io::SceneNodeDocument cameraNode;
  cameraNode.nodeName = "camera";
  cameraNode.name = "camera";
  cameraNode.camera = LX_infra::scene_io::CameraNodeState{};
  root.children.push_back(std::move(cameraNode));

  LX_infra::scene_io::SceneNodeDocument objectNode;
  objectNode.nodeName = "cube";
  objectNode.name = "cube";
  objectNode.meshUri = "builtin://lxe_editor/primitives/cube";
  objectNode.materialUri = "assets/materials/pbr.material";
  root.children.push_back(std::move(objectNode));

  LX_infra::scene_io::SceneNodeDocument lightNode;
  lightNode.nodeName = "sun";
  lightNode.name = "sun";
  lightNode.light = LX_infra::scene_io::LightNodeState{};
  root.children.push_back(std::move(lightNode));

  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver{std::filesystem::current_path()}};
  const auto loaded = loader.load(document, "");
  const auto state = loaded.table.environmentRuntimeState();
  EXPECT(state.has_value() && state->nodePresent,
         "offline loader should register cache environment node state");
  if (!state.has_value()) {
    return;
  }
  EXPECT(!state->bakeRequested,
         "offline loader should preserve disabled cache bake request");
  EXPECT(loaded.table.metadata(state->feature).uri ==
             LX_core::ResourceUri(cacheFeaturePath.generic_string()),
         "offline loader should resolve cache:// feature URI before parsing");
}

void testOfflineSceneLoaderRejectsMultipleEnvironmentNodes() {
  const LX_core::ResourceUri featureUri = writeTempRenderPathGraph(
      "lxe_offline_multiple_environment_nodes.render-feature.yaml", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
level: shader
shader:
  uri: features/environment_lighting
parameters:
  environmentMap:
    kind: textureCube
    uri: assets/env/khronos/neutral/ggx/specular.ktx2
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
)");

  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("MultipleEnvironmentNodes");
  document.setGameplayCameraPath("/camera");
  auto &root = document.mutableRootNode();
  root.nodeName = "scene_root";
  root.name = "";

  for (int i = 0; i < 2; ++i) {
    LX_infra::scene_io::SceneNodeDocument environmentNode;
    environmentNode.nodeName = "environment_" + std::to_string(i);
    environmentNode.name = environmentNode.nodeName;
    environmentNode.environment = LX_core::SceneEnvironmentNode{
        .featureUri = featureUri,
        .bake = LX_core::SceneIblBakeMarker{.enabled = false},
    };
    root.children.push_back(std::move(environmentNode));
  }

  LX_infra::scene_io::SceneNodeDocument cameraNode;
  cameraNode.nodeName = "camera";
  cameraNode.name = "camera";
  cameraNode.camera = LX_infra::scene_io::CameraNodeState{};
  root.children.push_back(std::move(cameraNode));

  LX_infra::scene_io::SceneNodeDocument objectNode;
  objectNode.nodeName = "cube";
  objectNode.name = "cube";
  objectNode.meshUri = "builtin://lxe_editor/primitives/cube";
  objectNode.materialUri = "assets/materials/pbr.material";
  root.children.push_back(std::move(objectNode));

  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver{std::filesystem::current_path()}};
  expectThrowsContaining(
      [&] { (void)loader.load(document, ""); },
      "scene contains multiple environment nodes",
      "offline loader should reject multiple environment nodes");
}

void testSceneDocumentRejectsMaterialBakeMarker() {
  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_material_bake.scene.yaml", R"(
scene:
  name: MaterialBake
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: object
      name: object
      material:
        uri: assets/materials/standard-pbr/standard-pbr.material.yaml
        bake:
          enabled: true
)");
      },
      "nodes[].material.bake is not supported; use nodes[].bake.ibl.enabled",
      "material.bake should be rejected with the migration diagnostic");
}

void testSceneDocumentRejectsUnknownEnvironmentNodeField() {
  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_environment_unknown.scene.yaml", R"(
scene:
  name: UnknownEnvironmentField
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: sky
      name: sky
      environment:
        feature:
          uri: assets/effects/environment_lighting.render-feature.yaml
        bake:
          enabled: true
        ignored: true
)");
      },
      "nodes[].environment.ignored",
      "unknown environment-node fields should fail-fast with YAML path");
}

void testSceneDocumentRejectsIncompleteEnvironmentNode() {
  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_environment_missing_feature_uri.scene.yaml",
                             R"(
scene:
  name: MissingEnvironmentFeatureUri
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: sky
      name: sky
      environment:
        feature: {}
        bake:
          enabled: true
)");
      },
      "nodes[].environment.feature.uri",
      "missing environment.feature.uri should fail-fast with YAML path");

  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_environment_missing_bake_enabled.scene.yaml",
                             R"(
scene:
  name: MissingEnvironmentBakeEnabled
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: sky
      name: sky
      environment:
        feature:
          uri: assets/effects/environment_lighting.render-feature.yaml
        bake: {}
)");
      },
      "nodes[].environment.bake.enabled",
      "missing environment.bake.enabled should fail-fast with YAML path");
}

void testSceneDocumentRejectsEnvironmentNodeWrongTypesWithPaths() {
  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_environment_bake_enabled_type.scene.yaml",
                             R"(
scene:
  name: WrongEnvironmentBakeEnabledType
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: sky
      name: sky
      environment:
        feature:
          uri: assets/effects/environment_lighting.render-feature.yaml
        bake:
          enabled: not-a-bool
)");
      },
      "nodes[].environment.bake.enabled",
      "wrong environment.bake.enabled type should name YAML path");

  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_object_bake_enabled_type.scene.yaml", R"(
scene:
  name: WrongObjectBakeEnabledType
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: object
      name: object
      bake:
        ibl:
          enabled: not-a-bool
)");
      },
      "nodes[].bake.ibl.enabled",
      "wrong bake.ibl.enabled type should name YAML path");

  expectThrowsContaining(
      [] {
        (void)parseSceneYaml("lxe_environment_feature_uri_type.scene.yaml",
                             R"(
scene:
  name: WrongEnvironmentFeatureUriType
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: sky
      name: sky
      environment:
        feature:
          uri: [assets, effects]
        bake:
          enabled: true
)");
      },
      "nodes[].environment.feature.uri",
      "wrong environment.feature.uri type should name YAML path");
}

void testLegacySceneEnvironmentDoesNotSatisfyEnvironmentNode() {
  const auto document =
      parseSceneYaml("lxe_legacy_scene_environment.scene.yaml", R"(
scene:
  name: LegacyEnvironment
  environment:
    enabled: true
    hdrUri: assets/env/studio_small_03_2k.hdr
root:
  nodeName: scene_root
  name: ""
  children:
    - nodeName: object
      name: object
)");

  EXPECT(document.hasEnvironment(),
         "legacy scene.environment should remain top-level scene state");
  EXPECT(!legacySceneEnvironmentSatisfiesEnvironmentNode(document),
         "legacy scene.environment must not satisfy environment node");
}

void testSceneDocumentSavesEnvironmentNodeAndExplicitBakeFalse() {
  LX_infra::scene_io::SceneDocument document;
  auto &root = document.mutableRootNode();
  LX_infra::scene_io::SceneNodeDocument environmentNode;
  environmentNode.nodeName = "sky";
  environmentNode.name = "sky";
  environmentNode.environment = LX_core::SceneEnvironmentNode{
      .featureUri = LX_core::ResourceUri(
          "assets/effects/environment_lighting.render-feature.yaml"),
      .bake = LX_core::SceneIblBakeMarker{.enabled = false},
  };
  root.children.push_back(std::move(environmentNode));

  LX_infra::scene_io::SceneNodeDocument objectNode;
  objectNode.nodeName = "helmet";
  objectNode.name = "helmet";
  objectNode.bake.ibl = LX_core::SceneIblBakeMarker{.enabled = false};
  root.children.push_back(std::move(objectNode));

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "lxe_environment_node_round_trip.scene.yaml";
  LX_infra::scene_io::saveSceneDocument(path, document);
  const auto roundTripped = LX_infra::scene_io::loadSceneDocument(path);

  EXPECT(roundTripped.rootNode().children[0].environment.has_value(),
         "saved environment node should round-trip");
  EXPECT(!roundTripped.rootNode().children[0].environment->bake.enabled,
         "explicit environment bake.enabled false should round-trip");
  EXPECT(roundTripped.rootNode().children[1].bake.ibl.has_value(),
         "explicit object bake.ibl marker should round-trip");
  EXPECT(!roundTripped.rootNode().children[1].bake.ibl->enabled,
         "explicit object bake.ibl.enabled false should round-trip");
}

} // namespace

int main() {
  if (std::filesystem::path sourceRoot{LXE_SOURCE_DIR}; !sourceRoot.empty()) {
    std::filesystem::current_path(sourceRoot);
  }

  testRemovedLegacyFiniteBoxRuntimeTokensDoNotAppearInProductionRuntime();
  testRenderFeatureParsesPureEnvelope();
  testRenderFeatureParsesBindingMemberRequiredSchema();
  testRenderFeatureParsesTextureCubeUriParameter();
  testDefaultRenderFeatureAssetParses();
  testEnvironmentLightingRenderFeatureAssetParses();
  testEnvironmentLightingRegistrationUsesCurrentFeatureParameters();
  testForwardPassRenderFeatureAssetParses();
  testSkyboxRenderFeatureAssetParses();
  testBloomRenderFeatureAssetParses();
  testSurfaceLightingRenderFeatureAssetParses();
  testRenderFeatureShaderAbiValidationAcceptsShaderBindingsAndMembers();
  testSurfaceLightingShaderAbiValidationAcceptsSharedUboMembers();
  testRenderFeatureShaderAbiValidationAcceptsPassSpecializationConstants();
  testRenderFeatureShaderAbiValidationAcceptsVolatilePassUniformMembers();
  testProductionRenderFeatureAssetsValidateShaderAbi();
  testRenderFeatureRejectsMissingLevel();
  testRenderFeatureRejectsInvalidLevel();
  testRenderFeatureRejectsMalformedShader();
  testRenderFeatureRejectsAbiParameterWithoutShaderUri();
  testRenderFeatureRejectsPassBindingMemberAndSpecialization();
  testRenderFeatureRejectsVolatileValueAndSpecializationFields();
  testRenderFeatureAcceptsVolatilePassUniformField();
  testRenderFeatureRejectsMissingRequiredValue();
  testRenderFeatureRejectsWrongBoolAndNumericValueTypes();
  testRenderFeatureRequiredBufferUsesUriNotValue();
  testTextureResourceParserUsesDeclaredContentFormat();
  testMaterialParserAnnotatesTextureDependencyContent();
  testDefaultRenderPathGraphAssetParses();
  testDefaultDeferredRenderPathGraphAssetParses();
  testSurfaceLightingRenderPathAssetsDeclareBakeFacts();
  testRenderPathGraphAcceptsSurfaceLightingFeatureSources();
  testBakeRenderPathGraphAssetsParseAndCompile();
  testBakeRenderPathGraphAssetsResolveShaderPayloads();
  testRenderPathFeatureValidationRejectsManualGammaOnSrgbForwardTarget();
  testParserAdapterRejectsManualGammaOnSrgbForwardTarget();
  testRenderFeatureRejectsRenderFlowFields();
  testRenderFeatureResourcesAreExplicitlyNotImplemented();
  testRenderFeatureRejectsUnknownParameterFields();
  testRenderFeatureRejectsUnknownParameterField();
  testEnvironmentLightingFeatureRejectsMissingEnvironmentMapUri();
  testRenderPathGraphRejectsEmptyPassContracts();
  testBakeRenderPathGraphRejectsMissingPayloadDeclaration();
  testBakeRenderPathGraphRejectsPayloadMissingFormat();
  testBakeRenderPathGraphRejectsPayloadWithoutTarget();
  testBakeRenderPathGraphRejectsPayloadTargetFormatMismatch();
  testRenderPathGraphRejectsUnparsedAllowedLookingFields();
  testLegacyRenderEffectSchemaIsRejectedByNewParser();
  testParserAdapterRejectsMissingGraphShaderDependency();
  testParserAdapterRejectsRootUtilityShaderUri();
  testParserAdapterRejectsDirectShaderSourceUri();
  testParserAdapterResolvesRenderPathShaderUriForms();
  testParserAdapterLoadsEnvironmentFeatureTextureDependency();
  testDefaultRenderPathGraphAssetsResolveLiveShaderPayloads();
  testSceneDocumentParsesEnvironmentNodeAndBakeMarkers();
  testOfflineSceneLoaderRegistersEnvironmentNodeFeatureFromFileUri();
  testOfflineSceneLoaderResolvesCacheEnvironmentFeatureUri();
  testOfflineSceneLoaderRejectsMultipleEnvironmentNodes();
  testSceneDocumentRejectsMaterialBakeMarker();
  testSceneDocumentRejectsUnknownEnvironmentNodeField();
  testSceneDocumentRejectsIncompleteEnvironmentNode();
  testSceneDocumentRejectsEnvironmentNodeWrongTypesWithPaths();
  testLegacySceneEnvironmentDoesNotSatisfyEnvironmentNode();
  testSceneDocumentSavesEnvironmentNodeAndExplicitBakeFalse();
  if (g_failures != 0) {
    std::cerr << g_failures << " render feature parser checks failed\n";
    return 1;
  }
  return 0;
}
