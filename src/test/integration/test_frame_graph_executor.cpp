#include "backend/vulkan/vulkan_frame_graph_executor.hpp"

#include "core/frame_graph/frame_graph_executor.hpp"
#include "core/scene/ibl_bake_service.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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

IblBakeItem makeBakeServiceItem() {
  return IblBakeItem{
      .id = 1,
      .kind = IblBakeItemKind::EnvironmentLight,
      .key = EnvironmentIblBakeKey{
          .environmentMapUri = ResourceUri("memory://env/executor-test.hdr"),
          .sourceHash = "sha256:executor-test",
      },
      .bakeRenderPathUri = ResourceUri(
          "assets/render_paths/bake_environment_ibl.render-path.yaml"),
  };
}

class InterfaceProbeCacheStore final : public IblBakeCacheStore {
public:
  [[nodiscard]] IblBakeCacheCheckResult
  check(const IblBakeItem &) override {
    ++checkCount;
    return IblBakeCacheCheckResult::missing("manifest missing");
  }

  [[nodiscard]] IblBakeCacheWriteResult
  write(const IblBakeItem &,
        const FrameGraphExecutionResult &) override {
    ++writeCount;
    return IblBakeCacheWriteResult::success();
  }

  int checkCount = 0;
  int writeCount = 0;
};

class InterfaceProbeFrameGraphExecutor final : public FrameGraphExecutor {
public:
  [[nodiscard]] FrameGraphExecutionResult
  execute(const FrameGraphExecutionRequest &request) override {
    ++executeCount;
    sawRequest = true;
    sawGraph = request.graph != nullptr;
    return FrameGraphExecutionResult{.ok = true};
  }

  int executeCount = 0;
  bool sawRequest = false;
  bool sawGraph = false;
};

class ExecutorTestGpuResource final : public IGpuResource {
public:
  explicit ExecutorTestGpuResource(StringID bindingName)
      : m_bindingName(bindingName) {}

  ResourceType getType() const override { return ResourceType::StorageBuffer; }
  const void *getRawData() const override { return m_bytes; }
  u32 getByteSize() const override { return sizeof(m_bytes); }
  StringID getBindingName() const override { return m_bindingName; }

private:
  StringID m_bindingName;
  u8 m_bytes[16]{};
};

std::optional<IblBakeJobStatus>
waitForStoppedJob(IblBakeJobService &service, BakeJobId job) {
  for (int i = 0; i < 200; ++i) {
    const auto status = service.status(job);
    if (status.has_value() && !status->running) {
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return service.status(job);
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

void testImmediateReadbackRejectsIncompleteContract() {
  VulkanFrameGraphExecutor executor(VulkanFrameGraphExecutionTarget{
      .mode = VulkanFrameGraphExecutionMode::ImmediateSubmitReadback,
  });
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().readbacks.push_back(
      RenderInputDesc::Readback{
          .name = "offline.output",
          .target = {},
          .binding = StringID("OutputPixels"),
          .extent = Vec3u{0u, 1u, 1u},
      });

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  const FrameGraphExecutionResult result = executor.execute(request);
  expect(!result.ok, "immediate readback should reject incomplete contract");
  expect(hasDiagnostic(result, "readback target is required"),
         "diagnostic should require readback target");
  expect(hasDiagnostic(result, "readback descriptor resource is required"),
         "diagnostic should require readback resource");
  expect(hasDiagnostic(result, "readback extent is required"),
         "diagnostic should require readback extent");
}

void testRecordOnlyIgnoresReadbackCollection() {
  VulkanFrameGraphExecutor executor;
  FrameGraph graph = makeGraph();
  CompiledFrameGraph compiled = graph.compile();
  std::vector<PreparedFramePassWork> prepared;
  prepared.push_back(makePreparedWork());
  prepared.front().descs.front().readbacks.push_back(
      RenderInputDesc::Readback{
          .name = "offline.output",
          .target = {},
          .binding = {},
          .extent = Vec3u{0u, 0u, 0u},
      });

  FrameGraphExecutionRequest request;
  request.graph = &graph;
  request.compiled = &compiled;
  request.preparedPasses = prepared;

  const FrameGraphExecutionResult result = executor.execute(request);
  expect(result.ok, "record-only execution should ignore readback collection");
  expect(result.outputs.empty(),
         "record-only execution should not collect readback payloads");
}

void testIblBakeJobServiceUsesFrameGraphExecutorInterface() {
  auto cache = std::make_shared<InterfaceProbeCacheStore>();
  auto executor = std::make_shared<InterfaceProbeFrameGraphExecutor>();
  const IblBakeItem item = makeBakeServiceItem();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "service should start with interface executor");
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(), "service should report final status");
  expect(status->phase == IblBakeJobPhase::Complete,
         "service should complete through interface executor");
  expect(cache->checkCount == 1, "service should check cache before executor");
  expect(executor->executeCount == 1,
         "service should invoke FrameGraphExecutor interface");
  expect(executor->sawRequest, "executor should receive execution request");
  expect(!executor->sawGraph,
         "service smoke should not depend on a concrete Vulkan graph");
  expect(cache->writeCount == 1,
         "service should write cache after interface executor succeeds");
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
  testImmediateReadbackRejectsIncompleteContract();
  testRecordOnlyIgnoresReadbackCollection();
  testIblBakeJobServiceUsesFrameGraphExecutorInterface();
  return 0;
}
