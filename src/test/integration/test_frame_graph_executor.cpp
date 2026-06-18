#include "backend/vulkan/vulkan_frame_graph_executor.hpp"

#include "core/frame_graph/frame_graph_executor.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace LX_core;
using namespace LX_core::backend;

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

bool hasDiagnostic(const FrameGraphExecutionResult &result,
                   const std::string &message) {
  for (const std::string &diagnostic : result.diagnostics) {
    if (diagnostic.find(message) != std::string::npos) {
      return true;
    }
  }
  return false;
}

FramePass makePass(const char *name = "Forward") {
  FramePass pass;
  pass.name = StringID(name);
  pass.shaderUri = ResourceUri{"builtin:/executor-test.shader"};
  pass.target =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  return pass;
}

FrameGraph makeGraph() {
  FrameGraph graph;
  graph.addPass(makePass());
  return graph;
}

FrameGraph makeTwoPassGraph() {
  FrameGraph graph;
  graph.addPass(makePass("CustomSecond"));
  graph.addPass(makePass("CustomFirst"));
  return graph;
}

RenderInputDesc makeAcceptedDesc(StringID passName = StringID("Forward")) {
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.pass = passName;
  desc.shaderUri = StringID("builtin:/executor-test.shader");
  desc.pipelineBuildDesc.target =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  return desc;
}

PreparedFramePassWork makePreparedWork(StringID passName = StringID("Forward")) {
  PreparedFramePassWork work;
  work.passName = passName;
  auto input = std::make_unique<RenderDrawInput>();
  input->pass = passName;
  input->inputIndex = 0;
  work.inputs.push_back(std::move(input));
  work.descs.push_back(makeAcceptedDesc(passName));
  return work;
}

void expectRejects(const FrameGraphExecutionResult &result,
                   const std::string &message) {
  expect(!result.ok, "executor request should be rejected");
  expect(hasDiagnostic(result, message), message.c_str());
}

void testMissingFrameGraphRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());

  FrameGraphExecutionRequest request;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "frame graph is required");
}

void testMissingCompiledGraphRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "compiled graph is required");
}

void testMissingPreparedPassRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "prepared pass work missing");
}

void testRejectedInputDescRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().status = RenderInputStatus::Rejected;

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "input desc rejected");
}

void testMissingTypedPayloadRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().inputs.clear();

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "typed payload is required");
}

void testWrongPassTypedPayloadRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().inputs.front()->pass = StringID("WrongPass");

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "typed payload is required");
}

void testWrongKindTypedPayloadRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  auto compute = std::make_unique<RenderComputeInput>();
  compute->pass = StringID("Forward");
  compute->inputIndex = 0;
  prepared.front().inputs.front() = std::move(compute);

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "typed payload is required");
}

void testMissingShaderRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().shaderUri = {};

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "shader is required");
}

void testMissingSourceShaderRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  graph.getPasses().front().shaderUri = ResourceUri{};
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "shader is required");
}

void testMissingTargetFormatRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  graph.getPasses().front().target =
      RenderTargetDesc::offscreenColors({}, std::nullopt);
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "target format is required");
}

void testMissingPreparedTargetFormatRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().pipelineBuildDesc.target =
      RenderTargetDesc::offscreenColors({}, std::nullopt);

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "target format is required");
}

void testMismatchedPreparedPassRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().pass = StringID("WrongPass");

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "prepared pass contract mismatch");
}

void testMismatchedPreparedTargetRejected() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().pipelineBuildDesc.target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  expectRejects(executor.execute(request), "prepared pass target mismatch");
}

void testExecutorUsesCompiledPassNames() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeTwoPassGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork(StringID("CustomFirst")));
  prepared.push_back(makePreparedWork(StringID("CustomSecond")));

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  const FrameGraphExecutionResult result = executor.execute(request);
  expect(result.ok, "executor should accept prepared work matched by pass name");
}

} // namespace

int main() {
  testMissingFrameGraphRejected();
  testMissingCompiledGraphRejected();
  testMissingPreparedPassRejected();
  testRejectedInputDescRejected();
  testMissingTypedPayloadRejected();
  testWrongPassTypedPayloadRejected();
  testWrongKindTypedPayloadRejected();
  testMissingShaderRejected();
  testMissingSourceShaderRejected();
  testMissingTargetFormatRejected();
  testMissingPreparedTargetFormatRejected();
  testMismatchedPreparedPassRejected();
  testMismatchedPreparedTargetRejected();
  testExecutorUsesCompiledPassNames();
  return 0;
}
