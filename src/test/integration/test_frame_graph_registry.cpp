#include "core/asset/render_effect.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/render_pass_contract_validator.hpp"

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

RenderPassNode makePass(std::string name, std::vector<std::string> sources,
                        std::vector<std::string> targets) {
  RenderPassNode pass;
  pass.id = std::move(name);
  pass.shaderUri = "shaders/test.effect";
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Draw;
  pass.sources = std::move(sources);
  pass.targets = std::move(targets);
  pass.renderState.cullMode = CullMode::Back;
  pass.renderState.depthTestEnable = true;
  pass.renderState.depthWriteEnable = true;
  return pass;
}

bool diagnosticContains(const RenderPassContractValidationReport &report,
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

void testRenderPassContractValidatorRejectsUnknownResource() {
  RenderPathGraph graph;
  graph.name = "Forward";
  graph.passes.push_back(
      makePass("Forward", {"geometry.vertex", "missing.input"}, {"hdr.color"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "unknown source should fail validation");
  EXPECT(
      diagnosticContains(report, "Forward", "missing.input", "unknown source"),
      "diagnostic should mention missing input");
}

void testRenderPassContractValidatorRejectsDuplicateProducer() {
  RenderPathGraph graph;
  graph.name = "Deferred";
  graph.passes.push_back(
      makePass("GBufferA", {"geometry.vertex"}, {"gbuffer.albedo"}));
  graph.passes.push_back(
      makePass("GBufferB", {"geometry.vertex"}, {"gbuffer.albedo"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "duplicate target producer should fail validation");
  EXPECT(diagnosticContains(report, "GBufferB", "gbuffer.albedo"),
         "diagnostic should mention duplicate target");
}

void testRenderPassContractValidatorRejectsSourceWithoutProducer() {
  RenderPathGraph graph;
  graph.name = "Post";
  graph.passes.push_back(makePass("ToneMap", {"hdr.color"}, {"ldr.color"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "registered target source without producer should fail");
  EXPECT(diagnosticContains(report, "ToneMap", "hdr.color", "has no producer"),
         "diagnostic should mention missing producer");
}

void testRenderPassContractValidatorAcceptsEarlierProducedSource() {
  RenderPathGraph graph;
  graph.name = "Post";
  graph.passes.push_back(
      makePass("Forward", {"geometry.vertex"}, {"hdr.color"}));
  graph.passes.push_back(makePass("ToneMap", {"hdr.color"}, {"ldr.color"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(), "source produced by an earlier pass should pass");
}

void testRenderPassContractValidatorAcceptsOutOfOrderProducedSource() {
  RenderPathGraph graph;
  graph.name = "Post";
  graph.passes.push_back(makePass("ToneMap", {"hdr.color"}, {"ldr.color"}));
  graph.passes.push_back(
      makePass("Forward", {"geometry.vertex"}, {"hdr.color"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(),
         "source produced by a later declared pass should pass DAG validation");
}

void testRenderPassContractValidatorRejectsSelfOnlyFeedbackSource() {
  RenderPathGraph graph;
  graph.name = "Feedback";
  graph.passes.push_back(
      makePass("FeedbackPass", {"hdr.color"}, {"hdr.color"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "self-only read/write feedback should fail");
  EXPECT(diagnosticContains(report, "FeedbackPass", "hdr.color",
                            "has no producer"),
         "diagnostic should mention self-only feedback has no producer");
}

void testRenderPassContractValidatorAllowsWriteModeAppend() {
  RenderPathGraph graph;
  graph.name = "Lighting";
  auto first = makePass("LightA", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "append";
  auto second = makePass("LightB", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "append";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(), "append write mode should allow multiple producers");
}

void testRenderPassContractValidatorAllowsWriteModeBlend() {
  RenderPathGraph graph;
  graph.name = "Lighting";
  auto first = makePass("Opaque", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "blend";
  auto second = makePass("Transparent", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "blend";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(report.ok(), "blend write mode should allow multiple producers");
}

void testRenderPassContractValidatorRejectsAppendWhenTargetDoesNotAllowIt() {
  RenderPathGraph graph;
  graph.name = "Deferred";
  auto first = makePass("GBufferA", {"geometry.vertex"}, {"gbuffer.albedo"});
  first.writeMode = "append";
  auto second = makePass("GBufferB", {"geometry.vertex"}, {"gbuffer.albedo"});
  second.writeMode = "append";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "append write mode should not bypass target registry capability");
  EXPECT(diagnosticContains(report, "GBufferA", "gbuffer.albedo",
                            "writeMode 'append'"),
         "diagnostic should mention unsupported append write mode");
}

void testRenderPassContractValidatorRejectsImportedTargetWrite() {
  RenderPathGraph graph;
  graph.name = "InvalidImportedTarget";
  graph.passes.push_back(
      makePass("WritesCamera", {"geometry.vertex"}, {"camera.ubo"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "imported/source-only target write should fail");
  EXPECT(diagnosticContains(report, "WritesCamera", "camera.ubo"),
         "diagnostic should mention imported target");
  EXPECT(diagnosticContains(report, "WritesCamera", "imported"),
         "diagnostic should explain target is imported/source-only");
}

void testRenderPassContractValidatorRejectsInvalidWriteModeOnSingleWriter() {
  RenderPathGraph graph;
  graph.name = "InvalidWriteMode";
  auto pass = makePass("Forward", {"geometry.vertex"}, {"hdr.color"});
  pass.writeMode = "overwrite-plus";
  graph.passes.push_back(std::move(pass));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "single writer with unsupported writeMode should fail");
  EXPECT(diagnosticContains(report, "Forward", "hdr.color", "overwrite-plus"),
         "diagnostic should mention invalid writeMode");
}

void testRenderPassContractValidatorRejectsSamePassDuplicateTarget() {
  RenderPathGraph graph;
  graph.name = "SamePassDuplicate";
  auto pass =
      makePass("Forward", {"geometry.vertex"}, {"hdr.color", "hdr.color"});
  pass.writeMode = "blend";
  graph.passes.push_back(std::move(pass));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "same pass duplicate target should fail even with allowed writeMode");
  EXPECT(diagnosticContains(report, "Forward", "hdr.color", "duplicate target"),
         "diagnostic should mention duplicate target");
}

void testRenderPassContractValidatorRejectsMixedDuplicateWriteMode() {
  RenderPathGraph graph;
  graph.name = "MixedDuplicateMode";
  auto first = makePass("BlendLighting", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "blend";
  auto second = makePass("AppendLighting", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "append";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "mixed duplicate writeMode should fail validation");
  EXPECT(
      diagnosticContains(report, "AppendLighting", "BlendLighting", "append"),
      "diagnostic should mention current pass, previous pass, and current "
      "writeMode");
  EXPECT(diagnosticContains(report, "AppendLighting", "BlendLighting", "blend"),
         "diagnostic should mention previous writeMode");
}

void testRenderPassContractValidatorReportsUnsupportedDuplicateWriteModeWithCurrentPass() {
  GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  registry.registerResource("custom.color");
  registry.allowWriteMode("custom.color", "blend");

  RenderPathGraph graph;
  graph.name = "UnsupportedDuplicateMode";
  auto first = makePass("BlendWriter", {"geometry.vertex"}, {"custom.color"});
  first.writeMode = "blend";
  auto second = makePass("AppendWriter", {"geometry.vertex"}, {"custom.color"});
  second.writeMode = "append";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(graph, registry);
  EXPECT(!report.ok(), "unsupported duplicate writeMode should fail");
  EXPECT(diagnosticContains(report, "AppendWriter", "BlendWriter", "append"),
         "diagnostic should mention current pass, previous pass, and "
         "unsupported writeMode");
}

void testRenderPassContractValidatorRequiresEveryDuplicateProducerToDeclareWriteMode() {
  RenderPathGraph graph;
  graph.name = "Lighting";
  auto first = makePass("OpaqueLighting", {"geometry.vertex"}, {"hdr.color"});
  auto second =
      makePass("TransparentLighting", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "append";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "every producer of a multiply-written target must declare writeMode");
  EXPECT(diagnosticContains(report, "TransparentLighting", "OpaqueLighting",
                            "missing writeMode"),
         "diagnostic should mention missing writeMode");
}

void testRenderPassContractValidatorReportsCurrentInvalidWriteModeBeforeDuplicateMode() {
  RenderPathGraph graph;
  graph.name = "Deferred";
  auto first = makePass("GBufferA", {"geometry.vertex"}, {"gbuffer.albedo"});
  auto second = makePass("GBufferB", {"geometry.vertex"}, {"gbuffer.albedo"});
  second.writeMode = "append";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "current invalid writeMode should be reported before duplicate mode "
         "fallbacks");
  EXPECT(diagnosticContains(report, "GBufferB", "gbuffer.albedo",
                            "writeMode 'append'"),
         "diagnostic should mention current pass and illegal writeMode");
}

void testRenderPassContractValidatorReportsPreviousMissingWriteModeWithCurrentPass() {
  RenderPathGraph graph;
  graph.name = "Lighting";
  auto first = makePass("OpaqueLighting", {"geometry.vertex"}, {"hdr.color"});
  auto second =
      makePass("TransparentLighting", {"geometry.vertex"}, {"hdr.color"});
  second.writeMode = "blend";
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "duplicate writer should fail when previous producer lacks writeMode");
  EXPECT(diagnosticContains(report, "TransparentLighting", "OpaqueLighting",
                            "missing writeMode"),
         "diagnostic should include current pass and previous pass");
}

void testRenderPassContractValidatorReportsCurrentMissingWriteModeWithCurrentPass() {
  RenderPathGraph graph;
  graph.name = "Lighting";
  auto first = makePass("OpaqueLighting", {"geometry.vertex"}, {"hdr.color"});
  first.writeMode = "blend";
  auto second =
      makePass("TransparentLighting", {"geometry.vertex"}, {"hdr.color"});
  graph.passes.push_back(std::move(first));
  graph.passes.push_back(std::move(second));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(),
         "duplicate writer should fail when current producer lacks writeMode");
  EXPECT(diagnosticContains(report, "TransparentLighting", "OpaqueLighting",
                            "missing writeMode"),
         "diagnostic should include current pass and previous pass");
}

void testRenderPassContractValidatorRejectsExplicitUnknownTarget() {
  RenderPathGraph graph;
  graph.name = "UnknownTarget";
  graph.passes.push_back(
      makePass("BadTarget", {"geometry.vertex"}, {"freeform.output"}));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "unknown target should fail validation");
  EXPECT(diagnosticContains(report, "BadTarget", "freeform.output",
                            "unknown target"),
         "diagnostic should mention current pass and unknown target");
}

void testRenderPassContractValidatorRejectsUnsupportedRasterDispatch() {
  RenderPathGraph graph;
  graph.name = "Forward";
  auto pass = makePass("Forward", {"geometry.vertex"}, {"hdr.color"});
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Compute;
  graph.passes.push_back(std::move(pass));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "raster pass with compute dispatch should fail");
  EXPECT(diagnosticContains(report, "Forward", "unsupported dispatch"),
         "diagnostic should mention unsupported dispatch");
}

void testRenderPassContractValidatorRejectsUnsupportedComputeDispatch() {
  RenderPathGraph graph;
  graph.name = "Post";
  auto pass = makePass("ToneMap", {"hdr.color"}, {"ldr.color"});
  pass.stage = RenderPassStage::Compute;
  pass.dispatch = RenderPassDispatch::Draw;
  graph.passes.push_back(std::move(pass));

  const auto report = validateRenderPassContractResources(
      graph, GraphResourceRegistry::makeDefault());
  EXPECT(!report.ok(), "compute pass with draw dispatch should fail");
  EXPECT(diagnosticContains(report, "ToneMap", "unsupported dispatch"),
         "diagnostic should mention unsupported dispatch");
}

} // namespace

int main() {
  testRegistryAcceptsStandardResources();
  testRegistryMarksStandardImportedSources();
  testRenderPassContractValidatorRejectsUnknownResource();
  testRenderPassContractValidatorRejectsDuplicateProducer();
  testRenderPassContractValidatorRejectsSourceWithoutProducer();
  testRenderPassContractValidatorAcceptsEarlierProducedSource();
  testRenderPassContractValidatorAcceptsOutOfOrderProducedSource();
  testRenderPassContractValidatorRejectsSelfOnlyFeedbackSource();
  testRenderPassContractValidatorAllowsWriteModeAppend();
  testRenderPassContractValidatorAllowsWriteModeBlend();
  testRenderPassContractValidatorRejectsAppendWhenTargetDoesNotAllowIt();
  testRenderPassContractValidatorRejectsImportedTargetWrite();
  testRenderPassContractValidatorRejectsInvalidWriteModeOnSingleWriter();
  testRenderPassContractValidatorRejectsSamePassDuplicateTarget();
  testRenderPassContractValidatorRejectsMixedDuplicateWriteMode();
  testRenderPassContractValidatorReportsUnsupportedDuplicateWriteModeWithCurrentPass();
  testRenderPassContractValidatorRequiresEveryDuplicateProducerToDeclareWriteMode();
  testRenderPassContractValidatorReportsCurrentInvalidWriteModeBeforeDuplicateMode();
  testRenderPassContractValidatorReportsPreviousMissingWriteModeWithCurrentPass();
  testRenderPassContractValidatorReportsCurrentMissingWriteModeWithCurrentPass();
  testRenderPassContractValidatorRejectsExplicitUnknownTarget();
  testRenderPassContractValidatorRejectsUnsupportedRasterDispatch();
  testRenderPassContractValidatorRejectsUnsupportedComputeDispatch();
  if (g_failures != 0) {
    std::cerr << g_failures << " frame graph registry checks failed\n";
    return 1;
  }
  return 0;
}
