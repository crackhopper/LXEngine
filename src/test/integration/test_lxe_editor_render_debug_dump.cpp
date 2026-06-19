#include "editor/app/editor_session.hpp"
#include "core/frame_graph/frame_graph_executor.hpp"
#include "core/scene/ibl_bake_keys.hpp"
#include "core/scene/ibl_bake_service.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "editor/app/editor_log_file.hpp"
#include "editor/panels/console_panel.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

usize countOccurrences(const std::string &haystack,
                       const std::string &needle) {
  usize count = 0;
  usize offset = 0;
  while (!needle.empty()) {
    offset = haystack.find(needle, offset);
    if (offset == std::string::npos) {
      break;
    }
    ++count;
    offset += needle.size();
  }
  return count;
}

int fail(const std::string &message) {
  std::cerr << message << '\n';
  return 1;
}

void expect(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  expect(static_cast<bool>(in), "unable to read " + path.generic_string());
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::filesystem::path repoRootForTest() {
  std::filesystem::path root = std::filesystem::current_path();
  if (std::filesystem::exists(root / "assets")) {
    return root;
  }
  if (std::filesystem::exists(root.parent_path() / "assets")) {
    return root.parent_path();
  }
  return root;
}

void expectJsonObject(const std::string &json, const std::string &context) {
  expect(json.size() >= 2 && json.front() == '{' && json.back() == '}',
         context + " should be a JSON object: " + json);
}

void expectJsonFieldString(const std::string &json, const std::string &field,
                           const std::string &value,
                           const std::string &context) {
  expect(contains(json, "\"" + field + "\":\"" + value + "\""),
         context + " should include string field " + field + "=" + value +
             ": " + json);
}

void expectJsonFieldBool(const std::string &json, const std::string &field,
                         bool value, const std::string &context) {
  expect(contains(json,
                  "\"" + field + "\":" + std::string(value ? "true" : "false")),
         context + " should include bool field " + field + ": " + json);
}

void expectJsonFieldNumber(const std::string &json, const std::string &field,
                           u64 value, const std::string &context) {
  expect(contains(json, "\"" + field + "\":" + std::to_string(value)),
         context + " should include numeric field " + field + ": " + json);
}

LX_core::IblBakeItem makeCommandBakeItem() {
  return LX_core::IblBakeItem{
      .id = 1,
      .kind = LX_core::IblBakeItemKind::EnvironmentLight,
      .key =
          LX_core::EnvironmentIblBakeKey{
              .environmentMapUri = LX_core::ResourceUri("memory://env/cmd.hdr"),
              .sourceHash = "sha256:cmd",
          },
      .bakeRenderPathUri = LX_core::ResourceUri(
          "assets/render_paths/bake_environment_ibl.render-path.yaml"),
  };
}

class BlockingCacheStore final : public LX_core::IblBakeCacheStore {
public:
  void blockNextCheck() {
    std::lock_guard lock(m_mutex);
    m_blockCheck = true;
    m_checkEntered = false;
    m_releaseCheck = false;
  }

  bool waitUntilCheckEntered() {
    std::unique_lock lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::seconds(2),
                         [&] { return m_checkEntered; });
  }

  void releaseBlockedCheck() {
    {
      std::lock_guard lock(m_mutex);
      m_releaseCheck = true;
    }
    m_cv.notify_all();
  }

  [[nodiscard]] LX_core::IblBakeCacheCheckResult
  check(const LX_core::IblBakeItem &) override {
    {
      std::unique_lock lock(m_mutex);
      if (m_blockCheck) {
        m_checkEntered = true;
        m_cv.notify_all();
        m_cv.wait(lock, [&] { return m_releaseCheck; });
        m_blockCheck = false;
      }
    }
    return LX_core::IblBakeCacheCheckResult::missing("manifest missing");
  }

  [[nodiscard]] LX_core::IblBakeCacheWriteResult
  write(const LX_core::IblBakeItem &,
        const LX_core::FrameGraphExecutionResult &) override {
    return LX_core::IblBakeCacheWriteResult::success();
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_blockCheck = false;
  bool m_checkEntered = false;
  bool m_releaseCheck = false;
};

class FakeFrameGraphExecutor final : public LX_core::FrameGraphExecutor {
public:
  [[nodiscard]] LX_core::FrameGraphExecutionResult
  execute(const LX_core::FrameGraphExecutionRequest &) override {
    return LX_core::FrameGraphExecutionResult{.ok = true};
  }
};

class BlockingCacheReleaseGuard final {
public:
  explicit BlockingCacheReleaseGuard(std::shared_ptr<BlockingCacheStore> cache)
      : m_cache(std::move(cache)) {}

  ~BlockingCacheReleaseGuard() {
    if (m_cache) {
      m_cache->releaseBlockedCheck();
    }
  }

  BlockingCacheReleaseGuard(const BlockingCacheReleaseGuard &) = delete;
  BlockingCacheReleaseGuard &
  operator=(const BlockingCacheReleaseGuard &) = delete;

  void releaseNow() {
    if (m_cache) {
      m_cache->releaseBlockedCheck();
      m_cache.reset();
    }
  }

private:
  std::shared_ptr<BlockingCacheStore> m_cache;
};

std::optional<LX_core::IblBakeJobStatus>
waitForStoppedJob(LX_core::IblBakeJobService &service, LX_core::BakeJobId job) {
  for (int i = 0; i < 200; ++i) {
    const auto status = service.status(job);
    if (status.has_value() && !status->running) {
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return service.status(job);
}

void appendBakeEventsToConsoleAndLog(
    LX_core::IblBakeJobService &service, LX_core::ConsolePanel &console,
    u64 &lastSequence) {
  const std::vector<LX_core::IblBakeJobEvent> events =
      service.events(lastSequence);
  for (const LX_core::IblBakeJobEvent &event : events) {
    std::ostringstream line;
    line << "[lxe_editor] bake job " << event.job << ' '
         << LX_core::iblBakeJobPhaseName(event.phase) << " progress="
         << std::fixed << std::setprecision(2) << event.progress
         << " seq=" << event.sequence;
    if (!event.message.empty()) {
      line << " message=" << event.message;
    }
    const std::string text = line.str();
    console.appendSystemLine(text);
    std::cerr << text << '\n';
    lastSequence = event.sequence;
  }
}

void testBakeCommandsReturnStructuredJsonWithoutEventSideEffects() {
  LX_core::CommandBus bus;
  LX_core::ConsolePanel console(bus);
  const std::filesystem::path logPath =
      repoRootForTest() / ".tmp_lxe_editor_bake_command.log";
  std::filesystem::remove(logPath);

  auto cache = std::make_shared<BlockingCacheStore>();
  cache->blockNextCheck();
  LX_core::IblBakeJobService service(LX_core::IblBakeJobServiceConfig{
      .items = {makeCommandBakeItem()},
      .cacheStore = cache,
      .executor = std::make_shared<FakeFrameGraphExecutor>(),
  });
  BlockingCacheReleaseGuard cacheGuard(cache);
  LX_demo::lxe_editor::registerBakeCommands(bus, service);

  {
    LX_demo::lxe_editor::ScopedEditorLogFile scopedLog(logPath);
    const LX_core::CommandResult usage = bus.dispatch("bake job status");
    expect(!usage.ok, "missing status id should fail");
    expectJsonObject(usage.structured, "usage error JSON");
    expectJsonFieldBool(usage.structured, "ok", false, "usage error JSON");
    expectJsonFieldString(usage.structured, "usage", "bake job status <id>",
                          "usage error JSON");

    const LX_core::CommandResult start = bus.dispatch("bake ibl start");
    expect(start.ok, "bake ibl start should succeed");
    expectJsonObject(start.structured, "start JSON");
    expectJsonFieldString(start.structured, "command", "bake ibl start",
                          "start JSON");
    expectJsonFieldNumber(start.structured, "job", 1, "start JSON");
    expect(contains(start.structured, "\"phase\":\""),
           "start JSON should include current phase: " + start.structured);
    expect(contains(start.structured, "\"sequence\":{\"first\":1,\"last\":"),
           "start JSON should include observed sequence range: " +
               start.structured);
    expect(contains(start.structured, "\"statusSequence\":"),
           "start JSON should expose status sequence separately: " +
               start.structured);

    expect(cache->waitUntilCheckEntered(),
           "duplicate test should reach blocking cache check");
    const LX_core::CommandResult duplicate = bus.dispatch("bake ibl start");
    expect(!duplicate.ok, "duplicate start should fail while job is running");
    expectJsonObject(duplicate.structured, "duplicate JSON");
    expectJsonFieldBool(duplicate.structured, "alreadyRunning", true,
                        "duplicate JSON");
    expectJsonFieldNumber(duplicate.structured, "job", 1, "duplicate JSON");

    const LX_core::CommandResult force = bus.dispatch("bake ibl start --force");
    expect(!force.ok, "force start should fail while job is running");
    expectJsonObject(force.structured, "force rejection JSON");
    expectJsonFieldBool(force.structured, "alreadyRunning", true,
                        "force rejection JSON");
    expectJsonFieldBool(force.structured, "rejected", true,
                        "force rejection JSON");
    expectJsonFieldNumber(force.structured, "job", 1, "force rejection JSON");

    const LX_core::CommandResult status = bus.dispatch("bake job status 1");
    expect(status.ok, "bake job status should succeed");
    expectJsonObject(status.structured, "status JSON");
    expectJsonFieldString(status.structured, "command", "bake job status",
                          "status JSON");
    expectJsonFieldString(status.structured, "phase", "cache-check",
                          "status JSON");
    expect(contains(status.structured, "\"statusSequence\":"),
           "status JSON should include status sequence: " + status.structured);

    const LX_core::CommandResult logs = bus.dispatch("bake job logs 1 1");
    expect(logs.ok, "bake job logs since sequence should succeed");
    expect(contains(logs.structured, "\"since\":1"),
           "logs JSON should include since sequence");
    expect(contains(logs.structured, "\"phase\":\"cache-check\""),
           "logs JSON should include later phase events");
    expect(!contains(logs.structured, "\"phase\":\"queued\""),
           "logs JSON should exclude events at or before since sequence");

    const LX_core::CommandResult cancel = bus.dispatch("bake job cancel 1");
    expect(cancel.ok, "bake job cancel should succeed");
    expect(contains(cancel.structured, "\"command\":\"bake job cancel\""),
           "cancel JSON should name the command");
    expect(contains(cancel.structured, "\"phase\":\"cancel-pending\""),
           "cancel JSON should include cancel phase");
    expect(contains(cancel.structured, "\"statusSequence\":"),
           "cancel JSON should expose status sequence separately: " +
               cancel.structured);

    expect(!contains(console.displayedText(), "[lxe_editor] bake job 1"),
           "commands should not pipe bake events into console directly");
  }

  const std::string logText = readTextFile(logPath);
  std::filesystem::remove(logPath);
  cacheGuard.releaseNow();
  (void)service.status(1);
  expect(!contains(logText, "[lxe_editor] bake job 1"),
         "commands should not pipe bake events into editor log directly");
}

void testBakeEventCursorAndSessionStylePumpAreMonotonic() {
  LX_core::CommandBus bus;
  LX_core::ConsolePanel console(bus);
  auto cache = std::make_shared<BlockingCacheStore>();
  cache->blockNextCheck();
  LX_core::IblBakeJobService service(LX_core::IblBakeJobServiceConfig{
      .items = {makeCommandBakeItem()},
      .cacheStore = cache,
      .executor = std::make_shared<FakeFrameGraphExecutor>(),
  });
  LX_demo::lxe_editor::registerBakeCommands(bus, service);
  BlockingCacheReleaseGuard cacheGuard(cache);

  const LX_core::IblBakeStartResult start = service.start(false);
  expect(start.ok, "cursor test should start bake job");
  u64 lastSequence = 0;
  appendBakeEventsToConsoleAndLog(service, console, lastSequence);
  expect(lastSequence >= 1, "first pump should emit queued event");

  expect(cache->waitUntilCheckEntered(),
         "cursor test should reach blocking cache check");
  appendBakeEventsToConsoleAndLog(service, console, lastSequence);
  const u64 cacheCheckSequence = lastSequence;
  expect(contains(console.displayedText(), "bake job 1 cache-check"),
         "second pump should emit async cache-check phase");
  appendBakeEventsToConsoleAndLog(service, console, lastSequence);
  expect(lastSequence == cacheCheckSequence,
         "empty pump should preserve last emitted sequence");
  expect(service.events(lastSequence).empty(),
         "events cursor should not repeat events at the last sequence");

  (void)bus.dispatch("bake job logs 1 0");
  (void)bus.dispatch("bake job logs 1 0");
  appendBakeEventsToConsoleAndLog(service, console, lastSequence);
  expect(countOccurrences(console.displayedText(), "bake job 1 queued") == 1,
         "repeated log queries should not duplicate pumped queued line");
  expect(countOccurrences(console.displayedText(), "bake job 1 cache-check") ==
             1,
         "repeated log queries should not duplicate pumped cache-check line");

  cacheGuard.releaseNow();
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value() && !status->running,
         "cursor test job should stop after release");
  const std::vector<LX_core::IblBakeJobEvent> later =
      service.events(lastSequence);
  expect(!later.empty(), "events cursor should return later async phases");
  for (const LX_core::IblBakeJobEvent &event : later) {
    lastSequence = event.sequence;
  }
  expect(service.events(lastSequence).empty(),
         "events cursor should not repeat final async phases");
}

} // namespace

int main() {
  try {
    testBakeCommandsReturnStructuredJsonWithoutEventSideEffects();
    testBakeEventCursorAndSessionStylePumpAreMonotonic();
  } catch (const std::exception &error) {
    return fail(error.what());
  }

  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks;

  std::filesystem::path requestedPath;
  std::filesystem::path requestedScreenPath;
  int dumpCallCount = 0;
  hooks.dumpRenderTarget =
      [&requestedPath, &requestedScreenPath, &dumpCallCount](
          std::string_view targetName, const std::filesystem::path &path,
          const std::filesystem::path &screenPath)
      -> LX_demo::lxe_editor::LxeEditorSession::RenderDebugDumpResult {
    ++dumpCallCount;
    if (targetName != "hdr.color") {
      throw std::runtime_error("unexpected target");
    }
    requestedPath = path;
    requestedScreenPath = screenPath;
    return LX_demo::lxe_editor::LxeEditorSession::RenderDebugDumpResult{
        .path = path,
        .screenPath = screenPath,
        .width = 16,
        .height = 16,
        .format = "R16G16B16A16_SFLOAT",
        .maxValue = 1.0,
        .meanValue = 0.5,
        .nonZeroRatio = 1.0,
    };
  };

  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const LX_core::CommandResult result =
      bus.dispatch("render debug dump hdr.color");
  if (!result.ok) {
    return fail("render debug dump command failed: " + result.message);
  }
  if (requestedPath.extension() != ".png") {
    return fail("default debug dump path should use .png, got: " +
                requestedPath.generic_string());
  }
  if (requestedScreenPath.extension() != ".png") {
    return fail("paired screen dump path should use .png, got: " +
                requestedScreenPath.generic_string());
  }
  if (requestedScreenPath.parent_path() != requestedPath.parent_path() ||
      requestedScreenPath.stem().generic_string() !=
          requestedPath.stem().generic_string() + "-screen") {
    return fail("paired screen dump path should share the target path stem: " +
                requestedScreenPath.generic_string());
  }
  if (!contains(result.structured, "\"path\":\"") ||
      !contains(result.structured, ".png\"")) {
    return fail("structured dump result should report a png path: " +
                result.structured);
  }
  if (!contains(result.structured, "\"screenPath\":\"") ||
      !contains(result.structured, "-screen.png\"")) {
    return fail("structured dump result should report a paired screen path: " +
                result.structured);
  }

  const LX_core::CommandResult passDumpResult =
      bus.dispatch("render debug dump Forward");
  if (passDumpResult.ok) {
    return fail("render debug dump Forward should be rejected");
  }
  if (!contains(passDumpResult.message, "pass dump")) {
    return fail("rejected pass dump should explain the removed path: " +
                passDumpResult.message);
  }
  if (dumpCallCount != 1) {
    return fail("rejected pass dump should not call backend dump hook");
  }

  hooks.initSceneCallCount = []() -> usize { return 3; };
  hooks.preparedRenderWorkDiagnostics =
      []() -> LX_core::backend::PreparedRenderWorkDiagnostics {
    return LX_core::backend::PreparedRenderWorkDiagnostics{
        .frameGraphCompileCount = 4,
        .renderInputBuildCount = 5,
        .renderInputPrepareCount = 6,
        .descriptorUploadPlanBuildCount = 7,
        .uploadPlanSyncCount = 8,
        .volatileUploadSyncCount = 9,
        .cachedUploadResourceTouchCount = 10,
    };
  };
  const LX_core::CommandResult perfStatsResult =
      bus.dispatch("render debug perf-stats");
  if (!perfStatsResult.ok) {
    return fail("render debug perf-stats command failed: " +
                perfStatsResult.message);
  }
  if (!contains(perfStatsResult.structured, "\"initSceneCallCount\":3") ||
      !contains(perfStatsResult.structured, "\"frameGraphCompileCount\":4") ||
      !contains(perfStatsResult.structured, "\"renderInputBuildCount\":5") ||
      !contains(perfStatsResult.structured,
                "\"descriptorUploadPlanBuildCount\":7") ||
      !contains(perfStatsResult.structured,
                "\"cachedUploadResourceTouchCount\":10")) {
    return fail("render debug perf-stats should return prepared-work JSON: " +
                perfStatsResult.structured);
  }

  const LX_core::backend::VulkanPostProcessSettings settings;
  if (settings.bloomEnabled) {
    return fail("bloom should be disabled by default");
  }

  return 0;
}
