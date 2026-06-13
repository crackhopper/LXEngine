#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/resource_parsers/render_feature_resource_parser.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"

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
  EXPECT(graph.features.size() == 1,
         "default forward graph should reference tone mapping feature");
  EXPECT(graph.passes.size() == 4,
         "default forward graph should declare shadow, opaque, tone-map, and "
         "debug overlay passes");
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
           "default forward graph should run HDR producer after shadow");
    EXPECT(compiled.getPasses()[2].name == LX_core::StringID("PostProcess"),
           "default forward graph should run tone map after HDR producer");
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
  EXPECT(graph.passes.size() == 5,
         "default deferred graph should declare shadow, GBuffer, lighting, "
         "post-process, and debug overlay passes");
  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "default deferred graph asset should compile into a FrameGraph plan");
  if (compiled.getPasses().size() == 5) {
    EXPECT(compiled.getPasses()[0].name == LX_core::StringID("Shadow"),
           "default deferred graph should run shadow pass first");
    EXPECT(compiled.getPasses()[1].name == LX_core::StringID("Deferred"),
           "default deferred graph should run GBuffer producer after shadow");
    EXPECT(compiled.getPasses()[2].name ==
               LX_core::StringID("DeferredLighting"),
           "default deferred graph should run lighting after GBuffer");
    EXPECT(compiled.getPasses()[3].name == LX_core::StringID("PostProcess"),
           "default deferred graph should run post process after lighting");
    EXPECT(compiled.getPasses()[4].name == LX_core::StringID("DebugOverlay"),
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
shader: techniques/Forward/tone_map
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
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
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
    shader: techniques/Forward/pbr
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
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
    shader: techniques/Forward/pbr
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
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
    shader: techniques/Forward/pbr
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte]
      ignoredFilter: true
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
  EXPECT(hasDiagnosticContaining(parsed, "passes.Forward.filters.ignoredFilter"),
         "diagnostic should reject unknown filter field");
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
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
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

void testParserAdapterResolvesCurrentGraphShaderUriForms() {
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
    shader: techniques/Forward/pbr
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
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
    shader: techniques/Deferred/gbuffer
    rendering:
      mode: dynamic
      attachments:
        - target: gbuffer.albedo
          format: RGBA8
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
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
    shader: deferred_lighting
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
    shader: post_process
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
    shader: bloom_threshold
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
    shader: bloom_blur_h
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
    shader: bloom_blur_v
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
         "current graph shader URI forms should resolve without diagnostics");
  EXPECT(parsed.identity.isValid(),
         "graph with current shader URI forms should register");
  EXPECT(table.shaderCount() == 7,
         "each current shader URI form should register a typed shader");

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

void testDefaultRenderPathGraphAssetsResolveLiveShaderPayloads() {
  struct ExpectedAsset final {
    const char *path;
    std::size_t shaderCount;
  };
  const ExpectedAsset assets[] = {
      {"assets/render_paths/forward_main.render-path.yaml", 4},
      {"assets/render_paths/forward_bloom.render-path.yaml", 7},
      {"assets/render_paths/deferred_main.render-path.yaml", 5},
      {"assets/render_paths/deferred_bloom.render-path.yaml", 8},
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
  testDefaultRenderFeatureAssetParses();
  testDefaultRenderPathGraphAssetParses();
  testDefaultDeferredRenderPathGraphAssetParses();
  testRenderFeatureRejectsPassAndShaderFields();
  testRenderFeatureResourcesAreExplicitlyNotImplemented();
  testRenderFeatureRejectsUnknownParameterFields();
  testRenderPathGraphRejectsEmptyPassContracts();
  testRenderPathGraphRejectsUnparsedAllowedLookingFields();
  testLegacyRenderEffectSchemaIsRejectedByNewParser();
  testParserAdapterRejectsMissingGraphShaderDependency();
  testParserAdapterResolvesCurrentGraphShaderUriForms();
  testDefaultRenderPathGraphAssetsResolveLiveShaderPayloads();
  if (g_failures != 0) {
    std::cerr << g_failures << " render feature parser checks failed\n";
    return 1;
  }
  return 0;
}
