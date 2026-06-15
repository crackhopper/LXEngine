#include "infra/material_loader/pbrt_material_defaults.hpp"

#include <iostream>

using namespace LX_infra;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testDefaultsLoadEnvelopeValues() {
  const auto table = loadPbrtMaterialDefaultsFromYaml(R"(
matte:
  Kd: { kind: rgb, value: [0.8, 0.8, 0.8] }
  sigma: { kind: float, value: 0.0 }
metal:
  eta: { kind: spectrum, uri: spds/Al.eta.spd }
  k: { kind: spectrum, uri: spds/Al.k.spd }
)");

  EXPECT(table.diagnostics.empty(), "valid defaults should not emit diagnostics");
  const auto kd = table.find("matte", "Kd");
  EXPECT(kd.has_value(), "matte.Kd default should exist");
  EXPECT(kd.has_value() && kd->get().kind == LX_core::MaterialEnvelopeKind::Rgb,
         "matte.Kd should retain rgb kind");
  EXPECT(kd.has_value() && kd->get().rgbValue.has_value(),
         "matte.Kd should retain rgb value");

  const auto eta = table.find("metal", "eta");
  EXPECT(eta.has_value(), "metal.eta default should exist");
  EXPECT(eta.has_value() && eta->get().uri == "spds/Al.eta.spd",
         "metal.eta should retain uri");
}

void testDefaultsRejectInvalidEnvelope() {
  const auto table = loadPbrtMaterialDefaultsFromYaml(R"(
matte:
  Kd: { kind: texture, uri: textures/kd.png }
)");

  EXPECT(!table.diagnostics.empty(),
         "invalid texture default without valueType should emit diagnostics");
  EXPECT(!table.find("matte", "Kd").has_value(),
         "invalid default should not be stored");
}

} // namespace

int main() {
  testDefaultsLoadEnvelopeValues();
  testDefaultsRejectInvalidEnvelope();
  if (g_failures != 0) {
    std::cerr << g_failures << " PBRT default checks failed\n";
    return 1;
  }
  return 0;
}
