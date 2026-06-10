#include "infra/resource_parsers/render_effect_resource_parser.hpp"

#include <iostream>

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testRejectsInvalidPhase() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://bad-phase", R"(
schema: lxe.render-effect.v1
phase: middle
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

  EXPECT(!parsed.effect.has_value(), "invalid phase should fail");
  EXPECT(!parsed.diagnostics.empty(), "invalid phase should emit diagnostics");
  EXPECT(!parsed.diagnostics.empty() &&
             parsed.diagnostics.front().find("phase") != std::string::npos,
         "diagnostic should include phase field path");
}

void testRejectsMissingRenderState() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse("memory://missing-render-state", R"(
schema: lxe.render-effect.v1
phase: pre
passes:
  ShadowPrep:
    shader: shaders/shadow.effect
    stage: raster
    dispatch: draw
    sources: [geometry.vertex]
    targets: [shadow.main]
)");

  EXPECT(!parsed.effect.has_value(), "missing renderState should fail");
  EXPECT(!parsed.diagnostics.empty(),
         "missing renderState should emit diagnostics");
  EXPECT(!parsed.diagnostics.empty() &&
             parsed.diagnostics.front().find("passes.ShadowPrep.renderState") !=
                 std::string::npos,
         "diagnostic should include renderState field path");
}

} // namespace

int main() {
  testRejectsInvalidPhase();
  testRejectsMissingRenderState();
  if (g_failures != 0) {
    std::cerr << g_failures << " render effect parser checks failed\n";
    return 1;
  }
  return 0;
}
