#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_resource_parser.hpp"
#include "infra/resource_parsers/render_feature_resource_parser.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"
#include "infra/resource_parsers/texture_resource_parser.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
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

LX_core::ResourceUri writeTempRenderPathGraph(const std::string &fileName,
                                              const std::string &contents) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / fileName;
  std::ofstream file(path);
  file << contents;
  return LX_core::ResourceUri("file://" + path.generic_string());
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

void testRenderFeatureParsesPureEnvelope() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://shadow-feature", R"(
schema: lxe.render-feature.v1
name: MainShadow
feature: shadowmap
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
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: infinite
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
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
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: infinite
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
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
  EXPECT(feature.parameters.find("exposure") != feature.parameters.end(),
         "default tone mapping feature should declare exposure");
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
  EXPECT(feature.parameters.size() == 6,
         "environment lighting feature should declare exactly six "
         "parameters");
  for (const char *name : {"environmentMap", "color", "intensity", "rotation",
                           "backgroundMode", "finiteBoxBounds"}) {
    EXPECT(feature.parameters.find(name) != feature.parameters.end(),
           std::string("environment lighting feature should declare ") + name);
  }
  EXPECT(feature.parameters.find("visibleInBackground") ==
             feature.parameters.end(),
         "environment lighting feature must hard-cut visibleInBackground");
  const auto backgroundMode = feature.parameters.find("backgroundMode");
  EXPECT(backgroundMode != feature.parameters.end() &&
             backgroundMode->second.value == "finiteBox",
         "environment lighting asset should select finiteBox background mode "
         "for box-scene validation");
  const auto finiteBoxBounds = feature.parameters.find("finiteBoxBounds");
  EXPECT(finiteBoxBounds != feature.parameters.end() &&
             finiteBoxBounds->second.kind == "vec6" &&
             !finiteBoxBounds->second.value.empty(),
         "finiteBox environment lighting asset should declare explicit bounds");
  EXPECT(finiteBoxBounds != feature.parameters.end() &&
             finiteBoxBounds->second.value ==
                 "[-20.0, 20.0, -8.0, 12.0, -20.0, 20.0]",
         "finiteBox environment lighting asset should use room-sized bounds");
  EXPECT(feature.parameters.find("skyboxEnabled") == feature.parameters.end(),
         "environment lighting feature must not declare skyboxEnabled");
  EXPECT(feature.parameters.find("ambientColor") == feature.parameters.end(),
         "environment lighting feature must not declare ambientColor");
  EXPECT(feature.parameters.find("ambientIntensity") ==
             feature.parameters.end(),
         "environment lighting feature must not declare ambientIntensity");
}

void testEnvironmentLightingFeatureParsesBackgroundModeAndFiniteBoxBounds() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-background-mode", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: finiteBox
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
  finiteBoxBounds:
    kind: vec6
    value: [-5.0, 5.0, -2.0, 3.0, -4.0, 4.0]
    requiredWhen:
      parameter: backgroundMode
      equals: finiteBox
)");

  EXPECT(parsed.renderFeature.has_value(),
         "environmentLighting backgroundMode/finiteBoxBounds should parse");
  EXPECT(parsed.diagnostics.empty(),
         "environmentLighting backgroundMode/finiteBoxBounds should not emit "
         "diagnostics");
  if (!parsed.renderFeature.has_value()) {
    return;
  }
  const auto &backgroundMode =
      parsed.renderFeature->parameters.at("backgroundMode");
  EXPECT(backgroundMode.kind == "enum", "backgroundMode kind should be enum");
  EXPECT(backgroundMode.value == "finiteBox",
         "backgroundMode value should be retained");
  EXPECT(backgroundMode.binding == "EnvironmentLightingUBO",
         "backgroundMode UBO binding should be retained");
  EXPECT(backgroundMode.member == "backgroundMode",
         "backgroundMode UBO member should be retained");
  EXPECT(backgroundMode.required,
         "backgroundMode required flag should be retained");
  EXPECT(backgroundMode.allowedValues.size() == 3,
         "backgroundMode allowed values should be retained");

  const auto &bounds = parsed.renderFeature->parameters.at("finiteBoxBounds");
  EXPECT(bounds.kind == "vec6", "finiteBoxBounds kind should be vec6");
  EXPECT(bounds.requiredWhenParameter == "backgroundMode",
         "finiteBoxBounds requiredWhen parameter should be retained");
  EXPECT(bounds.requiredWhenEquals == "finiteBox",
         "finiteBoxBounds requiredWhen equals should be retained");
}

void testEnvironmentLightingFeatureRejectsVisibleInBackground() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-visible-legacy", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: infinite
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
  visibleInBackground:
    kind: bool
    value: true
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "environmentLighting should hard-cut visibleInBackground");
  EXPECT(hasDiagnosticContaining(parsed, "visibleInBackground"),
         "diagnostic should name visibleInBackground");
}

void testEnvironmentLightingFeatureRejectsInvalidBackgroundMode() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-bad-mode", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: room
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "environmentLighting should reject unsupported backgroundMode");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.backgroundMode.value"),
         "diagnostic should name backgroundMode value");
}

void testEnvironmentLightingFeatureRejectsFiniteBoxWithoutBounds() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-finite-no-bounds", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: finiteBox
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "finiteBox backgroundMode should require finiteBoxBounds");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.finiteBoxBounds"),
         "diagnostic should name finiteBoxBounds");
}

void testEnvironmentLightingFeatureRejectsInvalidFiniteBoxBounds() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://environment-finite-bad-bounds", R"(
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    binding: SkyboxMap
    required: true
  backgroundMode:
    kind: enum
    value: finiteBox
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
  finiteBoxBounds:
    kind: vec6
    value: [5.0, -5.0, -2.0, 3.0, -4.0, 4.0]
    requiredWhen:
      parameter: backgroundMode
      equals: finiteBox
)");

  EXPECT(!parsed.renderFeature.has_value(),
         "finiteBoxBounds should reject non-increasing x bounds");
  EXPECT(hasDiagnosticContaining(parsed, "parameters.finiteBoxBounds.value"),
         "diagnostic should name finiteBoxBounds value");
}

void testTextureResourceParserUsesDeclaredContentFormat() {
  LX_infra::TextureResourceParser parser;
  LX_core::SceneResourceTable colorTable;
  const LX_core::ResourceUri colorUri(
      "assets://models/damaged_helmet/Default_albedo.jpg");
  const auto parsedColor = parser.parse(
      colorTable, colorUri,
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
  const auto parsedNormal = parser.parse(
      normalTable, normalUri,
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
  const auto parsed = parser.parse(table, materialUri,
                                   readTextFile(materialUri.string()));
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
  EXPECT(graph.features.size() == 2,
         "default forward graph should reference tone mapping and environment "
         "features");
  EXPECT(graph.passes.size() == 3,
         "default forward graph should declare one shared Forward pass plus "
         "shadow and debug overlay");
  if (graph.passes.size() == 3) {
    EXPECT(graph.passes[1].id == "Forward",
           "default forward graph should keep background and opaque draws in "
           "the Forward pass");
    EXPECT(std::find(graph.passes[1].input.material.types.begin(),
                     graph.passes[1].input.material.types.end(),
                     "environment-box") !=
               graph.passes[1].input.material.types.end(),
           "default Forward pass should accept environment-box material draws");
    EXPECT(std::find(graph.passes[1].sources.begin(),
                     graph.passes[1].sources.end(), "feature.toneMapping") !=
               graph.passes[1].sources.end(),
           "default Forward pass should consume toneMapping feature directly");
    EXPECT(std::find(graph.passes[1].targets.begin(),
                     graph.passes[1].targets.end(), "swapchain.color") !=
               graph.passes[1].targets.end(),
           "default Forward pass should write final swapchain color");
  }
  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "default forward graph asset should compile into a FrameGraph plan");
  if (compiled.getPasses().size() == 3) {
    EXPECT(compiled.getPasses()[0].name == LX_core::StringID("Shadow"),
           "default forward graph should run shadow pass first");
    EXPECT(compiled.getPasses()[1].name == LX_core::StringID("Forward"),
           "default forward graph should run all scene draws after shadow");
    EXPECT(compiled.getPasses()[2].name == LX_core::StringID("DebugOverlay"),
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
  EXPECT(parsed.diagnostics.empty(),
         "default deferred render path graph asset should not emit diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }

  const auto &graph = *parsed.renderPathGraph;
  EXPECT(graph.name == "DeferredMain",
         "default deferred graph should retain name");
  EXPECT(graph.renderPath == LX_core::RenderPath::Deferred,
         "default deferred graph should use Deferred render path");
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
    EXPECT(std::find(graph.passes[2].sources.begin(),
                     graph.passes[2].sources.end(), "feature.toneMapping") !=
               graph.passes[2].sources.end(),
           "default deferred lighting pass should consume toneMapping feature "
           "directly");
    EXPECT(std::find(graph.passes[2].targets.begin(),
                     graph.passes[2].targets.end(), "swapchain.color") !=
               graph.passes[2].targets.end(),
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

void testRenderFeatureRejectsPassAndShaderFields() {
  LX_infra::RenderFeatureResourceParser parser;
  const auto parsed = parser.parse("memory://bad-feature", R"(
schema: lxe.render-feature.v1
name: BadFeature
feature: toneMapping
pass: ToneMap
shader: render_paths/Forward/tone_map
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
  EXPECT(hasDiagnosticContaining(parsed, "shader"),
         "diagnostic should reject shader");
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
  const auto parsed = parser.parse("memory://feature-parameter-extra-singular", R"(
schema: lxe.render-feature.v1
name: FeatureWithParameterExtra
feature: toneMapping
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
  const LX_core::ResourceUri graphUri = writeTempRenderPathGraph(
      "lxe_missing_shader.render-path.yaml", R"(
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

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

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

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

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

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

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
  const LX_core::ResourceUri graphUri = writeTempRenderPathGraph(
      "lxe_shader_forms.render-path.yaml", R"(
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

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

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
  backgroundMode:
    kind: enum
    value: infinite
    binding: EnvironmentLightingUBO
    member: backgroundMode
    required: true
    allowedValues: [none, infinite, finiteBox]
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
      {"assets/render_paths/forward_main.render-path.yaml", 3},
      {"assets/render_paths/forward_bloom.render-path.yaml", 3},
      {"assets/render_paths/deferred_main.render-path.yaml", 4},
      {"assets/render_paths/deferred_bloom.render-path.yaml", 4},
  };

  for (const ExpectedAsset &asset : assets) {
    LX_infra::SceneResourceParserRegistry registry;
    LX_infra::registerRenderResourceParsers(registry);
    LX_core::SceneResourceTable table;

    const auto parsed = registry.parse(
        table, LX_core::SceneResourceType::RenderPathGraph,
        LX_core::ResourceUri(asset.path), LX_infra::SceneResourceParseContext{});

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

} // namespace

int main() {
  if (std::filesystem::path sourceRoot{LXE_SOURCE_DIR}; !sourceRoot.empty()) {
    std::filesystem::current_path(sourceRoot);
  }

  testRenderFeatureParsesPureEnvelope();
  testRenderFeatureParsesBindingMemberRequiredSchema();
  testRenderFeatureParsesTextureCubeUriParameter();
  testDefaultRenderFeatureAssetParses();
  testEnvironmentLightingRenderFeatureAssetParses();
  testEnvironmentLightingFeatureParsesBackgroundModeAndFiniteBoxBounds();
  testEnvironmentLightingFeatureRejectsVisibleInBackground();
  testEnvironmentLightingFeatureRejectsInvalidBackgroundMode();
  testEnvironmentLightingFeatureRejectsFiniteBoxWithoutBounds();
  testEnvironmentLightingFeatureRejectsInvalidFiniteBoxBounds();
  testTextureResourceParserUsesDeclaredContentFormat();
  testMaterialParserAnnotatesTextureDependencyContent();
  testDefaultRenderPathGraphAssetParses();
  testDefaultDeferredRenderPathGraphAssetParses();
  testRenderFeatureRejectsPassAndShaderFields();
  testRenderFeatureResourcesAreExplicitlyNotImplemented();
  testRenderFeatureRejectsUnknownParameterFields();
  testRenderFeatureRejectsUnknownParameterField();
  testEnvironmentLightingFeatureRejectsMissingEnvironmentMapUri();
  testRenderPathGraphRejectsEmptyPassContracts();
  testRenderPathGraphRejectsUnparsedAllowedLookingFields();
  testLegacyRenderEffectSchemaIsRejectedByNewParser();
  testParserAdapterRejectsMissingGraphShaderDependency();
  testParserAdapterRejectsRootUtilityShaderUri();
  testParserAdapterRejectsDirectShaderSourceUri();
  testParserAdapterResolvesRenderPathShaderUriForms();
  testParserAdapterLoadsEnvironmentFeatureTextureDependency();
  testDefaultRenderPathGraphAssetsResolveLiveShaderPayloads();
  if (g_failures != 0) {
    std::cerr << g_failures << " render feature parser checks failed\n";
    return 1;
  }
  return 0;
}
