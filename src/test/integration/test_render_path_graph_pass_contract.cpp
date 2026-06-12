#include "core/asset/render_effect.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"

#include <iostream>
#include <string>

using namespace LX_core;

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

void testRenderPathGraphPassContractRequiresExplicitFields() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://missing-dispatch", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: ForwardOpaque
    shader: techniques/Forward/surface_lit
    stage: raster
    sources: [geometry.vertex, material.bsdf, scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: LessEqual
)");

  EXPECT(!parsed.renderPathGraph.has_value(), "missing dispatch should fail");
  EXPECT(!parsed.diagnostics.empty(), "missing dispatch should emit diagnostic");
  EXPECT(!parsed.diagnostics.empty() &&
             parsed.diagnostics.front().find("passes.ForwardOpaque.dispatch") !=
                 std::string::npos,
         "diagnostic should include missing dispatch field path");
}

void testRenderPathGraphContractParsesCompletePass() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://complete", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
features:
  shadow:
    uri: effects/shadow.render-feature.yaml
passes:
  - id: ForwardOpaque
    shader: techniques/Forward/surface_lit
    stage: raster
    dispatch: draw
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte, uber]
    sources: [geometry.vertex, material.bsdf, scene.camera, feature.shadow]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
      blendEnable: false
)");

  EXPECT(parsed.renderPathGraph.has_value(), "complete graph should parse");
  EXPECT(parsed.diagnostics.empty(), "complete graph should not emit diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }

  const auto &graph = *parsed.renderPathGraph;
  EXPECT(graph.name == "ForwardMain", "graph name should be retained");
  EXPECT(graph.renderPath == RenderPath::Forward,
         "render path should parse as Forward");
  EXPECT(graph.features.size() == 1, "one feature dependency should be parsed");
  EXPECT(graph.features.front().slot == "shadow",
         "feature slot should be retained");
  EXPECT(graph.features.front().uri == "effects/shadow.render-feature.yaml",
         "feature uri should be retained");
  EXPECT(graph.passes.size() == 1, "one render pass node should be parsed");
  const RenderPassNode &pass = graph.passes.front();
  EXPECT(pass.id == "ForwardOpaque", "pass id should be retained");
  EXPECT(pass.shaderUri == "techniques/Forward/surface_lit",
         "shader uri should be retained");
  EXPECT(pass.stage == RenderPassStage::Raster, "stage should be raster");
  EXPECT(pass.dispatch == RenderPassDispatch::Draw,
         "dispatch should be draw");
  EXPECT(pass.filters.renderClasses.size() == 1 &&
             pass.filters.renderClasses.front() == "surface.opaque",
         "renderClass filter should be retained");
  EXPECT(pass.filters.bsdfTypes.size() == 2 &&
             pass.filters.bsdfTypes.front() == "matte",
         "bsdf filter should be retained");
  EXPECT(pass.sources.size() == 4 && pass.sources.back() == "feature.shadow",
         "sources should be retained");
  EXPECT(pass.targets.size() == 2 && pass.targets.back() == "depth.main",
         "targets should be retained");
  EXPECT(pass.renderState.depthWriteEnable,
         "render state should be retained");
}

void testRenderPathGraphPassRejectsLegacyPassNodeFields() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://legacy-pass-fields", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: ForwardOpaque
    shader: techniques/Forward/surface_lit
    stage: raster
    dispatch: draw
    variants:
      quality: high
    parameters:
      roughness:
        value: 0.5
    resources:
      albedo:
        uri: textures/albedo.ktx
    sources: [geometry.vertex, material.bsdf, scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "legacy pass-node fields must fail schema gate");
  EXPECT(hasDiagnosticContaining(parsed, "passes.ForwardOpaque.variants"),
         "diagnostic should reject variants");
  EXPECT(hasDiagnosticContaining(parsed, "passes.ForwardOpaque.parameters"),
         "diagnostic should reject parameters");
  EXPECT(hasDiagnosticContaining(parsed, "passes.ForwardOpaque.resources"),
         "diagnostic should reject resources");
}

} // namespace

int main() {
  testRenderPathGraphPassContractRequiresExplicitFields();
  testRenderPathGraphContractParsesCompletePass();
  testRenderPathGraphPassRejectsLegacyPassNodeFields();
  if (g_failures != 0) {
    std::cerr << g_failures << " render path graph contract checks failed\n";
    return 1;
  }
  return 0;
}
