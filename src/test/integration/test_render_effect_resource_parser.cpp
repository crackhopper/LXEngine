#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "infra/resource_parsers/render_effect_resource_parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

bool hasDiagnosticContaining(const LX_infra::ParsedRenderEffectResource &parsed,
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

void testRenderFeatureParsesPureEnvelope() {
  LX_infra::RenderEffectResourceParser parser;
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
  EXPECT(!parsed.renderPathGraph.has_value(),
         "render feature parse should not create a render path graph");
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
  LX_infra::RenderEffectResourceParser parser;
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
  LX_infra::RenderEffectResourceParser parser;
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
  EXPECT(graph.passes.size() == 2,
         "default forward graph should declare opaque and tone-map passes");
  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "default forward graph asset should compile into a FrameGraph plan");
  if (compiled.getPasses().size() == 2) {
    EXPECT(compiled.getPasses()[0].name == LX_core::StringID("Forward"),
           "default forward graph should run HDR producer first");
    EXPECT(compiled.getPasses()[1].name == LX_core::StringID("PostProcess"),
           "default forward graph should run tone map after HDR producer");
  }
}

void testRenderFeatureRejectsPassAndShaderFields() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://bad-feature", R"(
schema: lxe.render-feature.v1
name: BadFeature
feature: toneMapping
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
  EXPECT(hasDiagnosticContaining(parsed, "passes"),
         "diagnostic should reject passes");
  EXPECT(hasDiagnosticContaining(parsed, "renderState"),
         "diagnostic should reject renderState");
}

void testRenderPathGraphRejectsEmptyPassContracts() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://bad-render-path", R"(
schema: lxe.render-path-graph.v1
name: BadForward
renderPath: Forward
passes:
  - id: EmptyShader
    stage: raster
    dispatch: draw
    shader: ""
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

void testLegacyRenderEffectSchemaIsRejectedByNewParser() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://legacy-effect", R"(
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

  EXPECT(!parsed.renderFeature.has_value(), "legacy effect should not parse");
  EXPECT(!parsed.renderPathGraph.has_value(),
         "legacy effect should not produce a graph");
  EXPECT(hasDiagnosticContaining(parsed, "lxe.render-path-graph.v1"),
         "diagnostic should point to new render path graph schema");
  EXPECT(hasDiagnosticContaining(parsed, "lxe.render-feature.v1"),
         "diagnostic should point to new render feature schema");
}

} // namespace

int main() {
  testRenderFeatureParsesPureEnvelope();
  testDefaultRenderFeatureAssetParses();
  testDefaultRenderPathGraphAssetParses();
  testRenderFeatureRejectsPassAndShaderFields();
  testRenderPathGraphRejectsEmptyPassContracts();
  testLegacyRenderEffectSchemaIsRejectedByNewParser();
  if (g_failures != 0) {
    std::cerr << g_failures << " render feature parser checks failed\n";
    return 1;
  }
  return 0;
}
