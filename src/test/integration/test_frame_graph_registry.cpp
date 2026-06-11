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

bool diagnosticContains(const TechniqueValidationReport &report,
                        const std::string &first,
                        const std::string &second = {},
                        const std::string &third = {}) {
  for (const auto &diagnostic : report.diagnostics) {
    if (diagnostic.find(first) == std::string::npos) {
      continue;
    }
    if (!second.empty() && diagnostic.find(second) == std::string::npos) {
      continue;
    }
    if (!third.empty() && diagnostic.find(third) == std::string::npos) {
      continue;
    }
    return true;
  }
  return false;
}

void testRegistryAcceptsStandardResources() {
  const GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  EXPECT(registry.contains("depth.main"), "depth.main should be registered");
  EXPECT(registry.contains("gbuffer.albedo"),
         "gbuffer.albedo should be registered");
  EXPECT(registry.contains("hdr.color"), "hdr.color should be registered");
  EXPECT(registry.contains("bloom.threshold"),
         "bloom.threshold should be registered");
  EXPECT(registry.contains("bloom.blurH"), "bloom.blurH should be registered");
  EXPECT(registry.contains("bloom.blur"), "bloom.blur should be registered");
  EXPECT(!registry.isImported("bloom.threshold"),
         "bloom.threshold should be writable, not imported");
  EXPECT(!registry.isImported("bloom.blurH"),
         "bloom.blurH should be writable, not imported");
  EXPECT(!registry.isImported("bloom.blur"),
         "bloom.blur should be writable, not imported");
  EXPECT(registry.contains("scene.bvh"), "scene.bvh should be registered");
  EXPECT(!registry.contains("unknown.buffer"),
         "unknown.buffer should not be registered");
}

void testRegistryMarksStandardImportedSources() {
  const GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  EXPECT(registry.isImported("geometry.vertex"),
         "geometry.vertex should be imported");
  EXPECT(registry.isImported("geometry.index"),
         "geometry.index should be imported");
  EXPECT(registry.isImported("material.bsdf"),
         "material.bsdf should be imported");
  EXPECT(registry.isImported("camera.ubo"), "camera.ubo should be imported");
  EXPECT(registry.isImported("scene.lights"),
         "scene.lights should be imported");
  EXPECT(registry.isImported("scene.bvh"), "scene.bvh should be imported");
  EXPECT(registry.isImported("scene.environment"),
         "scene.environment should be imported");
}

void testTechniqueValidatorRejectsUnknownResource() {
  MaterialTechnique technique;
  technique.name = "Forward";
  technique.passes.push_back(
      makePass("Forward", {"geometry.vertex", "missing.input"}, {"hdr.color"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "unknown source should fail validation");
  EXPECT(diagnosticContains(report, "Forward", "missing.input",
                            "unknown source"),
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
  EXPECT(diagnosticContains(report, "GBufferB", "gbuffer.albedo"),
         "diagnostic should mention duplicate target");
}

void testTechniqueValidatorRejectsSourceWithoutProducer() {
  MaterialTechnique technique;
  technique.name = "Post";
  technique.passes.push_back(
      makePass("ToneMap", {"hdr.color"}, {"ldr.color"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "registered target source without producer should fail");
  EXPECT(diagnosticContains(report, "ToneMap", "hdr.color",
                            "has no producer"),
         "diagnostic should mention missing producer");
}

void testTechniqueValidatorAcceptsEarlierProducedSource() {
  MaterialTechnique technique;
  technique.name = "Post";
  technique.passes.push_back(
      makePass("Forward", {"geometry.vertex"}, {"hdr.color"}));
  technique.passes.push_back(
      makePass("ToneMap", {"hdr.color"}, {"ldr.color"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(), "source produced by an earlier pass should pass");
}

void testTechniqueValidatorAcceptsOutOfOrderProducedSource() {
  MaterialTechnique technique;
  technique.name = "Post";
  technique.passes.push_back(
      makePass("ToneMap", {"hdr.color"}, {"ldr.color"}));
  technique.passes.push_back(
      makePass("Forward", {"geometry.vertex"}, {"hdr.color"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(),
         "source produced by a later declared pass should pass DAG validation");
}

void testTechniqueValidatorRejectsSelfOnlyFeedbackSource() {
  MaterialTechnique technique;
  technique.name = "Feedback";
  technique.passes.push_back(
      makePass("FeedbackPass", {"hdr.color"}, {"hdr.color"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "self-only read/write feedback should fail");
  EXPECT(diagnosticContains(report, "FeedbackPass", "hdr.color",
                            "has no producer"),
         "diagnostic should mention self-only feedback has no producer");
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

void testTechniqueValidatorAllowsWriteModeBlend() {
  MaterialTechnique technique;
  technique.name = "Lighting";
  auto first = makePass("Opaque", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "blend";
  auto second = makePass("Transparent", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "blend";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(), "blend write mode should allow multiple producers");
}

void testTechniqueValidatorRejectsAppendWhenTargetDoesNotAllowIt() {
  MaterialTechnique technique;
  technique.name = "Deferred";
  auto first = makePass("GBufferA", {"geometry.vertex"}, {"gbuffer.albedo"});
  first.writeMode = "append";
  auto second = makePass("GBufferB", {"geometry.vertex"}, {"gbuffer.albedo"});
  second.writeMode = "append";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "append write mode should not bypass target registry capability");
  EXPECT(diagnosticContains(report, "GBufferA", "gbuffer.albedo",
                            "writeMode 'append'"),
         "diagnostic should mention unsupported append write mode");
}

void testTechniqueValidatorRejectsImportedTargetWrite() {
  MaterialTechnique technique;
  technique.name = "InvalidImportedTarget";
  technique.passes.push_back(
      makePass("WritesCamera", {"geometry.vertex"}, {"camera.ubo"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "imported/source-only target write should fail");
  EXPECT(diagnosticContains(report, "WritesCamera", "camera.ubo"),
         "diagnostic should mention imported target");
  EXPECT(diagnosticContains(report, "WritesCamera", "imported"),
         "diagnostic should explain target is imported/source-only");
}

void testTechniqueValidatorRejectsInvalidWriteModeOnSingleWriter() {
  MaterialTechnique technique;
  technique.name = "InvalidWriteMode";
  auto pass = makePass("Forward", {"geometry.vertex"}, {"hdr.color"});
  pass.writeMode = "overwrite-plus";
  technique.passes.push_back(std::move(pass));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "single writer with unsupported writeMode should fail");
  EXPECT(diagnosticContains(report, "Forward", "hdr.color", "overwrite-plus"),
         "diagnostic should mention invalid writeMode");
}

void testTechniqueValidatorRejectsSamePassDuplicateTarget() {
  MaterialTechnique technique;
  technique.name = "SamePassDuplicate";
  auto pass =
      makePass("Forward", {"geometry.vertex"}, {"hdr.color", "hdr.color"});
  pass.writeMode = "blend";
  technique.passes.push_back(std::move(pass));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "same pass duplicate target should fail even with allowed writeMode");
  EXPECT(diagnosticContains(report, "Forward", "hdr.color",
                            "duplicate target"),
         "diagnostic should mention duplicate target");
}

void testTechniqueValidatorRejectsMixedDuplicateWriteMode() {
  MaterialTechnique technique;
  technique.name = "MixedDuplicateMode";
  auto first = makePass("BlendLighting", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "blend";
  auto second = makePass("AppendLighting", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "append";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "mixed duplicate writeMode should fail validation");
  EXPECT(diagnosticContains(report, "AppendLighting", "BlendLighting",
                            "append"),
         "diagnostic should mention current pass, previous pass, and current "
         "writeMode");
  EXPECT(diagnosticContains(report, "AppendLighting", "BlendLighting",
                            "blend"),
         "diagnostic should mention previous writeMode");
}

void testTechniqueValidatorReportsUnsupportedDuplicateWriteModeWithCurrentPass() {
  GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  registry.registerResource("custom.color");
  registry.allowWriteMode("custom.color", "blend");

  MaterialTechnique technique;
  technique.name = "UnsupportedDuplicateMode";
  auto first = makePass("BlendWriter", {"geometry.vertex"}, {"custom.color"});
  first.writeMode = "blend";
  auto second = makePass("AppendWriter", {"geometry.vertex"}, {"custom.color"});
  second.writeMode = "append";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report = validateTechniqueResources(technique, registry);
  EXPECT(!report.ok(), "unsupported duplicate writeMode should fail");
  EXPECT(diagnosticContains(report, "AppendWriter", "BlendWriter", "append"),
         "diagnostic should mention current pass, previous pass, and "
         "unsupported writeMode");
}

void testTechniqueValidatorRequiresEveryDuplicateProducerToDeclareWriteMode() {
  MaterialTechnique technique;
  technique.name = "Lighting";
  auto first = makePass("OpaqueLighting", {"geometry.vertex"}, {"hdr.color"});
  auto second =
      makePass("TransparentLighting", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "append";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "every producer of a multiply-written target must declare writeMode");
  EXPECT(diagnosticContains(report, "TransparentLighting", "OpaqueLighting",
                            "missing writeMode"),
         "diagnostic should mention missing writeMode");
}

void testTechniqueValidatorReportsCurrentInvalidWriteModeBeforeDuplicateMode() {
  MaterialTechnique technique;
  technique.name = "Deferred";
  auto first = makePass("GBufferA", {"geometry.vertex"}, {"gbuffer.albedo"});
  auto second = makePass("GBufferB", {"geometry.vertex"}, {"gbuffer.albedo"});
  second.writeMode = "append";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "current invalid writeMode should be reported before duplicate mode "
         "fallbacks");
  EXPECT(diagnosticContains(report, "GBufferB", "gbuffer.albedo",
                            "writeMode 'append'"),
         "diagnostic should mention current pass and illegal writeMode");
}

void testTechniqueValidatorReportsPreviousMissingWriteModeWithCurrentPass() {
  MaterialTechnique technique;
  technique.name = "Lighting";
  auto first = makePass("OpaqueLighting", {"geometry.vertex"}, {"hdr.color"});
  auto second =
      makePass("TransparentLighting", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "blend";
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "duplicate writer should fail when previous producer lacks writeMode");
  EXPECT(diagnosticContains(report, "TransparentLighting", "OpaqueLighting",
                            "missing writeMode"),
         "diagnostic should include current pass and previous pass");
}

void testTechniqueValidatorReportsCurrentMissingWriteModeWithCurrentPass() {
  MaterialTechnique technique;
  technique.name = "Lighting";
  auto first = makePass("OpaqueLighting", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "blend";
  auto second =
      makePass("TransparentLighting", {"geometry.vertex"}, {"hdr.color"});
  technique.passes.push_back(std::move(first));
  technique.passes.push_back(std::move(second));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "duplicate writer should fail when current producer lacks writeMode");
  EXPECT(diagnosticContains(report, "TransparentLighting", "OpaqueLighting",
                            "missing writeMode"),
         "diagnostic should include current pass and previous pass");
}

void testTechniqueValidatorRejectsExplicitUnknownTarget() {
  MaterialTechnique technique;
  technique.name = "UnknownTarget";
  technique.passes.push_back(
      makePass("BadTarget", {"geometry.vertex"}, {"freeform.output"}));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "unknown target should fail validation");
  EXPECT(diagnosticContains(report, "BadTarget", "freeform.output",
                            "unknown target"),
         "diagnostic should mention current pass and unknown target");
}

void testTechniqueValidatorRejectsUnsupportedRasterDispatch() {
  MaterialTechnique technique;
  technique.name = "Forward";
  auto pass = makePass("Forward", {"geometry.vertex"}, {"hdr.color"});
  pass.stage = MaterialPassStage::Raster;
  pass.dispatch = MaterialPassDispatch::Compute;
  technique.passes.push_back(std::move(pass));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "raster pass with compute dispatch should fail");
  EXPECT(diagnosticContains(report, "Forward", "unsupported dispatch"),
         "diagnostic should mention unsupported dispatch");
}

void testTechniqueValidatorRejectsUnsupportedComputeDispatch() {
  MaterialTechnique technique;
  technique.name = "Post";
  auto pass = makePass("ToneMap", {"hdr.color"}, {"ldr.color"});
  pass.stage = MaterialPassStage::Compute;
  pass.dispatch = MaterialPassDispatch::Draw;
  technique.passes.push_back(std::move(pass));

  const auto report =
      validateTechniqueResources(technique, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "compute pass with draw dispatch should fail");
  EXPECT(diagnosticContains(report, "ToneMap", "unsupported dispatch"),
         "diagnostic should mention unsupported dispatch");
}

} // namespace

int main() {
  testRegistryAcceptsStandardResources();
  testRegistryMarksStandardImportedSources();
  testTechniqueValidatorRejectsUnknownResource();
  testTechniqueValidatorRejectsDuplicateProducer();
  testTechniqueValidatorRejectsSourceWithoutProducer();
  testTechniqueValidatorAcceptsEarlierProducedSource();
  testTechniqueValidatorAcceptsOutOfOrderProducedSource();
  testTechniqueValidatorRejectsSelfOnlyFeedbackSource();
  testTechniqueValidatorAllowsWriteModeAppend();
  testTechniqueValidatorAllowsWriteModeBlend();
  testTechniqueValidatorRejectsAppendWhenTargetDoesNotAllowIt();
  testTechniqueValidatorRejectsImportedTargetWrite();
  testTechniqueValidatorRejectsInvalidWriteModeOnSingleWriter();
  testTechniqueValidatorRejectsSamePassDuplicateTarget();
  testTechniqueValidatorRejectsMixedDuplicateWriteMode();
  testTechniqueValidatorReportsUnsupportedDuplicateWriteModeWithCurrentPass();
  testTechniqueValidatorRequiresEveryDuplicateProducerToDeclareWriteMode();
  testTechniqueValidatorReportsCurrentInvalidWriteModeBeforeDuplicateMode();
  testTechniqueValidatorReportsPreviousMissingWriteModeWithCurrentPass();
  testTechniqueValidatorReportsCurrentMissingWriteModeWithCurrentPass();
  testTechniqueValidatorRejectsExplicitUnknownTarget();
  testTechniqueValidatorRejectsUnsupportedRasterDispatch();
  testTechniqueValidatorRejectsUnsupportedComputeDispatch();
  if (g_failures != 0) {
    std::cerr << g_failures << " frame graph registry checks failed\n";
    return 1;
  }
  return 0;
}
