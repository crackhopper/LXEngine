#include "core/asset/material_technique_set.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/technique_validator.hpp"

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

MaterialPassContract makePass(std::string name, std::vector<std::string> sources,
                              std::vector<std::string> targets) {
  MaterialPassContract pass;
  pass.name = std::move(name);
  pass.shaderUri = "shaders/test.effect";
  pass.stage = MaterialPassStage::Raster;
  pass.dispatch = MaterialPassDispatch::Draw;
  pass.sources = std::move(sources);
  pass.targets = std::move(targets);
  pass.renderState.cullMode = CullMode::Back;
  pass.renderState.depthTestEnable = true;
  pass.renderState.depthWriteEnable = true;
  return pass;
}

void testRegistryAcceptsStandardResources() {
  const GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  EXPECT(registry.contains("depth.main"), "depth.main should be registered");
  EXPECT(registry.contains("gbuffer.albedo"),
         "gbuffer.albedo should be registered");
  EXPECT(registry.contains("hdr.color"), "hdr.color should be registered");
  EXPECT(registry.contains("scene.bvh"), "scene.bvh should be registered");
  EXPECT(!registry.contains("unknown.buffer"),
         "unknown.buffer should not be registered");
}

void testTechniqueValidatorRejectsUnknownResource() {
  MaterialTechnique technique;
  technique.name = "Forward";
  technique.passes.push_back(
      makePass("Forward", {"geometry.vertex", "missing.input"}, {"hdr.color"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "unknown source should fail validation");
  EXPECT(!report.diagnostics.empty() &&
             report.diagnostics.front().find("missing.input") !=
                 std::string::npos,
         "diagnostic should mention missing input");
}

void testTechniqueValidatorRejectsDuplicateProducer() {
  MaterialTechnique technique;
  technique.name = "Deferred";
  technique.passes.push_back(
      makePass("GBufferA", {"geometry.vertex"}, {"gbuffer.albedo"}));
  technique.passes.push_back(
      makePass("GBufferB", {"geometry.vertex"}, {"gbuffer.albedo"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "duplicate target producer should fail validation");
  EXPECT(!report.diagnostics.empty() &&
             report.diagnostics.front().find("gbuffer.albedo") !=
                 std::string::npos,
         "diagnostic should mention duplicate target");
}

void testTechniqueValidatorAllowsWriteModeAppend() {
  MaterialTechnique technique;
  technique.name = "Lighting";
  auto first = makePass("LightA", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "append";
  auto second = makePass("LightB", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "append";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(), "append write mode should allow multiple producers");
}

} // namespace

int main() {
  testRegistryAcceptsStandardResources();
  testTechniqueValidatorRejectsUnknownResource();
  testTechniqueValidatorRejectsDuplicateProducer();
  testTechniqueValidatorAllowsWriteModeAppend();
  if (g_failures != 0) {
    std::cerr << g_failures << " frame graph registry checks failed\n";
    return 1;
  }
  return 0;
}
