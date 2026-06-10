#include "core/asset/material_technique_set.hpp"
#include "infra/resource_parsers/render_effect_resource_parser.hpp"

#include <iostream>

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

void testTechniquePassContractRequiresExplicitFields() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://missing-dispatch", R"(
schema: lxe.render-effect.v1
phase: post
passes:
  Composite:
    shader: shaders/composite.effect
    stage: raster
    sources: [hdr.color]
    targets: [swapchain.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
)");

  EXPECT(!parsed.effect.has_value(), "missing dispatch should fail");
  EXPECT(!parsed.diagnostics.empty(), "missing dispatch should emit diagnostic");
  EXPECT(!parsed.diagnostics.empty() &&
             parsed.diagnostics.front().find("passes.Composite.dispatch") !=
                 std::string::npos,
         "diagnostic should include missing dispatch field path");
}

void testTechniquePassContractParsesCompletePass() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://complete", R"(
schema: lxe.render-effect.v1
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
)");

  EXPECT(parsed.effect.has_value(), "complete pass should parse");
  EXPECT(parsed.diagnostics.empty(), "complete pass should not emit diagnostics");
  if (!parsed.effect.has_value()) {
    return;
  }

  const auto &effect = *parsed.effect;
  EXPECT(effect.phase == RenderEffectPhase::Post, "phase should be post");
  EXPECT(effect.technique.passes.size() == 1, "one pass should be parsed");
  const MaterialPassContract &pass = effect.technique.passes.front();
  EXPECT(pass.name == "Composite", "pass name should be retained");
  EXPECT(pass.shaderUri == "shaders/composite.effect",
         "shader uri should be retained");
  EXPECT(pass.stage == MaterialPassStage::Raster, "stage should be raster");
  EXPECT(pass.dispatch == MaterialPassDispatch::Fullscreen,
         "dispatch should be fullscreen");
  EXPECT(pass.sources.size() == 1 && pass.sources.front() == "hdr.color",
         "sources should be retained");
  EXPECT(pass.targets.size() == 1 && pass.targets.front() == "swapchain.color",
         "targets should be retained");
}

} // namespace

int main() {
  testTechniquePassContractRequiresExplicitFields();
  testTechniquePassContractParsesCompletePass();
  if (g_failures != 0) {
    std::cerr << g_failures << " technique pass contract checks failed\n";
    return 1;
  }
  return 0;
}
