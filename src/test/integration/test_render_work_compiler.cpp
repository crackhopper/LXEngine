#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_queue.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                     \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "[FAIL] " << msg << '\n';                                  \
      ++g_failures;                                                           \
    }                                                                         \
  } while (0)

template <typename T>
concept HasDrawCommands = requires(T value) { value.drawCommands; };

template <typename T>
concept HasGroupCounts = requires(T value) {
  value.groupCountX;
  value.groupCountY;
  value.groupCountZ;
};

template <typename T>
concept HasReadbackResource = requires(T value) { value.readbackResource; };

std::string readFile(const std::string &path) {
  std::ifstream file(path);
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

void testDescReferencesInputWithoutOwningPayload() {
  RenderDrawInput input;
  input.inputIndex = 0;
  input.source = RenderDrawInputSource::FullscreenTriangle;
  input.drawCommands.push_back(RenderDrawCommand{.indexCount = 3});

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;

  EXPECT(desc.accepted(), "desc should expose accepted state");
  EXPECT(desc.inputIndex == 0, "desc should reference input by index");
  EXPECT(input.drawCommands.size() == 1,
         "draw command payload should remain on input");
  EXPECT(!HasDrawCommands<RenderInputDesc>,
         "desc should not own draw command payload");
}

void testDescStatusDefaultsAndStats() {
  RenderInputDesc desc;
  EXPECT(!desc.accepted(), "desc should default to rejected");
  EXPECT(desc.status == RenderInputStatus::Rejected,
         "desc status should default to rejected");

  desc.status = RenderInputStatus::Accepted;
  EXPECT(desc.accepted(), "desc accepted() should track accepted status");

  desc.diagnostics.push_back(RenderInputDiagnostic{
      .code = RenderInputDiagnosticCode::MissingPipelineFacts,
      .pass = StringID("Forward"),
      .debugId = StringID("diagnostic-input"),
      .message = "missing pipeline facts",
  });
  desc.stats.inputCount = 2;
  desc.stats.acceptedInputCount = 1;
  desc.stats.rejectedInputCount = 1;
  desc.stats.submittedDrawCount = 3;
  desc.stats.submittedDispatchCount = 0;
  desc.stats.fallbackObservedCount = 1;

  EXPECT(desc.diagnostics.size() == 1,
         "desc should carry input diagnostics");
  EXPECT(desc.diagnostics.front().code ==
             RenderInputDiagnosticCode::MissingPipelineFacts,
         "desc diagnostic code should round-trip");
  EXPECT(desc.stats.inputCount == 2, "desc stats should carry input count");
  EXPECT(desc.stats.acceptedInputCount == 1,
         "desc stats should carry accepted input count");
  EXPECT(desc.stats.rejectedInputCount == 1,
         "desc stats should carry rejected input count");
  EXPECT(desc.stats.submittedDrawCount == 3,
         "desc stats should carry submitted draw count");
  EXPECT(desc.stats.submittedDispatchCount == 0,
         "desc stats should carry submitted dispatch count");
  EXPECT(desc.stats.fallbackObservedCount == 1,
         "desc stats should carry fallback observation count");
}

void testComputePayloadRemainsOnInput() {
  RenderComputeInput input;
  input.inputIndex = 7;
  input.groupCountX = 4;
  input.groupCountY = 5;
  input.groupCountZ = 6;
  input.readbackResource = StringID("readback.output");

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;

  EXPECT(input.kind() == RenderInputKind::Compute,
         "compute input should report compute kind");
  EXPECT(input.groupCountX == 4 && input.groupCountY == 5 &&
             input.groupCountZ == 6,
         "compute dispatch payload should remain on input");
  EXPECT(input.readbackResource.has_value(),
         "compute readback payload should remain on input");
  EXPECT(desc.accepted(), "accepted desc should indicate submit eligibility");
  EXPECT(desc.inputIndex == 7, "desc should reference compute input by index");
  EXPECT(!HasGroupCounts<RenderInputDesc>,
         "desc should not own compute dispatch payload");
  EXPECT(!HasReadbackResource<RenderInputDesc>,
         "desc should not own compute readback payload");
}

void testDescCarriesPipelineFacts() {
  RenderInputDesc desc;
  desc.pipelineKey = PipelineKey{StringID("pipeline.forward")};
  desc.shaderUri = StringID("shader://forward");
  desc.shaderVariantKey = StringID("variant.pbr");
  desc.reflectionIdentity = StringID("reflection.forward");
  desc.bindingPlan.descriptors.push_back(DescriptorResourceRef{});
  desc.resourceDependencies.push_back(GpuResourceRef{});

  EXPECT(desc.pipelineKey == PipelineKey{StringID("pipeline.forward")},
         "desc should carry pipeline key");
  EXPECT(desc.shaderUri == StringID("shader://forward"),
         "desc should carry shader uri");
  EXPECT(desc.shaderVariantKey == StringID("variant.pbr"),
         "desc should carry shader variant key");
  EXPECT(desc.reflectionIdentity == StringID("reflection.forward"),
         "desc should carry reflection identity");
  EXPECT(desc.bindingPlan.descriptors.size() == 1,
         "desc should carry descriptor binding plan");
  EXPECT(desc.resourceDependencies.size() == 1,
         "desc should carry resource dependencies");
}

void testRenderInputHeaderDoesNotDependOnLegacyQueueTokens() {
  const std::string header = readFile(
      std::string(LXE_SOURCE_DIR) + "/src/core/frame_graph/render_input.hpp");

  EXPECT(!header.empty(), "render_input.hpp source should be readable");
  EXPECT(header.find("render_queue") == std::string::npos,
         "render_input.hpp should not include legacy queue headers");
  EXPECT(header.find("RenderQueueDrawInput") == std::string::npos,
         "render_input.hpp should not reference legacy queue draw DTO");
  EXPECT(header.find("RenderWorkQueue") == std::string::npos,
         "render_input.hpp should not reference legacy queue type");
  EXPECT(header.find("RenderWorkItem") == std::string::npos,
         "render_input.hpp should not reference legacy work items");
  EXPECT(header.find("RenderBatch") == std::string::npos,
         "render_input.hpp should not reference legacy batches");
}

} // namespace

int main() {
  testDescReferencesInputWithoutOwningPayload();
  testDescStatusDefaultsAndStats();
  testComputePayloadRemainsOnInput();
  testDescCarriesPipelineFacts();
  testRenderInputHeaderDoesNotDependOnLegacyQueueTokens();
  return g_failures == 0 ? 0 : 1;
}
