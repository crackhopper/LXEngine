#include "core/asset/material_instance.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

#include <iostream>
#include <string_view>

using namespace LX_core;

namespace {

int g_failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    ++g_failures;
  }
}

MaterialInstanceSharedPtr parseDefaultMaterial(SceneResourceTable &table) {
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, "memory://default.material", R"(
schema: lxe.material.v2
bsdf:
  type: uber
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }
    Ks: { kind: rgb, value: [0.04, 0.04, 0.04] }
    normalmap: { kind: texture, valueType: rgb, uri: textures/normal.png }
)");

  expect(parsed.instance != nullptr, "valid v2 material should parse");
  expect(parsed.diagnostics.empty(),
         "valid v2 material should parse without diagnostics");
  if (!parsed.instance) {
    return nullptr;
  }

  return MaterialInstanceSharedPtr(std::move(parsed.instance));
}

void testEnvelopeState() {
  SceneResourceTable table;
  auto material = parseDefaultMaterial(table);
  if (!material) {
    return;
  }

  expect(material->getBsdfType() == "uber",
         "material instance should expose parsed BSDF type");
  expect(material->getMaterialEnvelopeCount() == 3,
         "material instance should expose parsed envelope table");

  const auto kd = material->getMaterialEnvelope(StringID("Kd"));
  expect(kd.has_value(), "Kd envelope should exist");
  expect(kd.has_value() && kd->get().kind == MaterialEnvelopeKind::Rgb,
         "Kd envelope should retain rgb kind");

  const auto normal = material->getMaterialEnvelope(StringID("normalmap"));
  expect(normal.has_value(), "normalmap envelope should exist");
  expect(normal.has_value() &&
             normal->get().kind == MaterialEnvelopeKind::Texture,
         "normalmap envelope should retain texture kind");
  expect(material->getMaterialDependencies().size() == 1,
         "texture envelope should produce one material dependency");
}

void testClonePreservesV2State() {
  SceneResourceTable table;
  auto material = parseDefaultMaterial(table);
  if (!material) {
    return;
  }

  auto clone = material->cloneInstanceData();
  expect(clone != nullptr, "clone should be created");
  expect(clone->getBsdfType() == material->getBsdfType(),
         "clone should preserve BSDF type");
  expect(clone->getMaterialEnvelopeCount() ==
             material->getMaterialEnvelopeCount(),
         "clone should preserve envelope count");
  expect(clone->getMaterialDependencies().size() ==
             material->getMaterialDependencies().size(),
         "clone should preserve dependency metadata");
}

} // namespace

int main() {
  testEnvelopeState();
  testClonePreservesV2State();

  if (g_failures != 0) {
    std::cerr << g_failures << " material instance checks failed\n";
    return 1;
  }
  std::cout << "material instance v2 checks passed\n";
  return 0;
}
