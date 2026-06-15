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
    shader: render_paths/Forward/surface_lit
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

void testRasterPassRejectsOldTopLevelFiltersAndGeometry() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://old-input", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: Forward
    shader: render_paths/Forward/pbr
    stage: raster
    dispatch: draw
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte]
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
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "old top-level filters and geometry must fail");
  EXPECT(hasDiagnosticContaining(parsed, "passes.Forward.filters"),
         "diagnostic should reject filters");
  EXPECT(hasDiagnosticContaining(parsed, "passes.Forward.geometry"),
         "diagnostic should reject geometry");
}

void testRenderPathGraphContractParsesCompletePass() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://complete", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
features:
  toneMapping:
    uri: effects/tone_mapping.render-feature.yaml
passes:
  - id: ForwardOpaque
    shader: render_paths/Forward/surface_lit
    stage: raster
    dispatch: draw
    input:
      kind: scene-renderables
      material:
        type: [matte, uber]
        required: true
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
        - target: depth.main
          format: D32Float
          samples: 1
          layers: 1
          depth: true
    sources: [geometry.vertex, material.bsdf, scene.camera, feature.toneMapping]
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
  EXPECT(graph.features.front().slot == "toneMapping",
         "feature slot should be retained");
  EXPECT(graph.features.front().uri == "effects/tone_mapping.render-feature.yaml",
         "feature uri should be retained");
  EXPECT(graph.passes.size() == 1, "one render pass node should be parsed");
  const RenderPassNode &pass = graph.passes.front();
  EXPECT(pass.id == "ForwardOpaque", "pass id should be retained");
  EXPECT(pass.shaderUri == "render_paths/Forward/surface_lit",
         "shader uri should be retained");
  EXPECT(pass.stage == RenderPassStage::Raster, "stage should be raster");
  EXPECT(pass.dispatch == RenderPassDispatch::Draw,
         "dispatch should be draw");
  EXPECT(pass.input.kind == RenderPassInputKind::SceneRenderables,
         "input kind should be scene-renderables");
  EXPECT(pass.input.material.types.size() == 2 &&
             pass.input.material.types.front() == "matte",
         "material type filter should be retained");
  EXPECT(pass.input.material.required,
         "material required flag should be retained");
  EXPECT(pass.input.geometry.has_value(),
         "scene-renderables input should retain geometry");
  EXPECT(pass.sources.size() == 4 && pass.sources.back() == "feature.toneMapping",
         "sources should be retained");
  EXPECT(pass.targets.size() == 2 && pass.targets.back() == "depth.main",
         "targets should be retained");
  EXPECT(pass.renderState.depthWriteEnable,
         "render state should be retained");
}

void testMaterialRequiredInvalidBooleanReportsDiagnostic() {
  LX_infra::RenderPathGraphResourceParser parser;
  LX_infra::ParsedRenderPathGraphResource parsed;
  bool threw = false;
  try {
    parsed = parser.parse("memory://invalid-required", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: Forward
    shader: render_paths/Forward/pbr
    stage: raster
    dispatch: draw
    input:
      kind: scene-renderables
      material:
        required: maybe
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
)");
  } catch (const std::exception &) {
    threw = true;
  }

  EXPECT(!threw, "invalid material.required must not escape as exception");
  EXPECT(!parsed.renderPathGraph.has_value(),
         "invalid material.required should fail parse");
  EXPECT(hasDiagnosticContaining(parsed, "passes.Forward.input.material.required"),
         "diagnostic should name invalid material.required field");
}

void testFullscreenTriangleInputParses() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://fullscreen", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: PostProcess
    shader: render_paths/Post/post_process
    stage: raster
    dispatch: fullscreen
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

  EXPECT(parsed.renderPathGraph.has_value(),
         "fullscreen-triangle input should parse");
  EXPECT(parsed.diagnostics.empty(),
         "fullscreen-triangle input should not emit diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }
  EXPECT(parsed.renderPathGraph->passes.front().input.kind ==
             RenderPassInputKind::FullscreenTriangle,
         "fullscreen input kind should be retained");
}

void testRenderPathGraphPassRejectsLegacyPassNodeFields() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://legacy-pass-fields", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: ForwardOpaque
    shader: render_paths/Forward/surface_lit
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
  testRasterPassRejectsOldTopLevelFiltersAndGeometry();
  testRenderPathGraphContractParsesCompletePass();
  testMaterialRequiredInvalidBooleanReportsDiagnostic();
  testFullscreenTriangleInputParses();
  testRenderPathGraphPassRejectsLegacyPassNodeFields();
  if (g_failures != 0) {
    std::cerr << g_failures << " render path graph contract checks failed\n";
    return 1;
  }
  return 0;
}
