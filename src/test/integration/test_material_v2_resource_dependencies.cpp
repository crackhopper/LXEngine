#include "core/asset/material_instance.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

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

void testParserResourceDependenciesSurviveTableRegistration() {
  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, "memory://dependency-material", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: texture, valueType: rgb, uri: textures/albedo.png }
    sigma: { kind: float, value: 0.0 }
)");

  EXPECT(parsed.instance != nullptr, "valid material should parse");
  EXPECT(parsed.diagnostics.empty(), "valid material should have no diagnostics");
  EXPECT(parsed.dependencies.size() == 1,
         "texture envelope should be reported as a dependency");
  if (!parsed.instance) {
    return;
  }

  const MaterialHandle handle =
      table.registerMaterial(std::move(parsed.instance));
  EXPECT(handle.isValid(), "parsed material should register into resource table");

  const auto material = table.resolve(handle);
  EXPECT(material.has_value(), "registered material should resolve");
  if (!material.has_value()) {
    return;
  }

  const auto &deps = material->get().getMaterialDependencies();
  EXPECT(deps.size() == 1,
         "registered material should retain dependency metadata");
  EXPECT(!deps.empty() && deps.front().uri.string() == "textures/albedo.png",
         "registered dependency should retain texture uri");
  EXPECT(!deps.empty() && deps.front().parameterName == "Kd",
         "registered dependency should retain parameter name");

  const auto kd = material->get().getMaterialEnvelope(StringID("Kd"));
  EXPECT(kd.has_value(), "registered material should retain Kd envelope");
  EXPECT(kd.has_value() && kd->get().kind == MaterialEnvelopeKind::Texture,
         "registered Kd envelope should retain texture kind");
}

} // namespace

int main() {
  testParserResourceDependenciesSurviveTableRegistration();
  if (g_failures != 0) {
    std::cerr << g_failures << " material v2 dependency checks failed\n";
    return 1;
  }
  return 0;
}
