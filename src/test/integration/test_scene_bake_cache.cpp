#include "core/scene/ibl_bake_job.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/lxe_editor_commands.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace LX_core;
using namespace LX_demo::lxe_editor;

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

void testEventSequenceIsMonotonic() {
  IblBakeEventQueue queue;
  const IblBakeJobEvent first =
      queue.push(IblBakeJobEvent{.job = 7,
                                 .phase = IblBakeJobPhase::Queued,
                                 .message = "queued"});
  const IblBakeJobEvent second =
      queue.push(IblBakeJobEvent{.job = 7,
                                 .phase = IblBakeJobPhase::CacheCheck,
                                 .message = "cache check"});
  expect(first.sequence + 1 == second.sequence,
         "event queue sequence should be monotonic");

  const std::vector<IblBakeJobEvent> afterFirst =
      queue.drainSince(first.sequence);
  expect(afterFirst.size() == 1, "drainSince should filter old events");
  expect(afterFirst.front().sequence == second.sequence,
         "drainSince should return later events");
}

void testRunningJobGuardRejectsForce() {
  IblBakeJobService service;

  const IblBakeStartResult start = service.start(false);
  expect(start.ok, "start should return ok");
  expect(start.job != 0, "start should return job id");

  const IblBakeStartResult second = service.start(false);
  expect(!second.ok, "duplicate start should not start a new job");
  expect(second.alreadyRunning, "duplicate start should report running job");
  expect(second.job == start.job, "duplicate start should return running job");

  const IblBakeStartResult forced = service.start(true);
  expect(!forced.ok, "force while running should be rejected");
  expect(forced.rejected, "force while running should report rejection");
  expect(contains(forced.message, "already running"),
         "force rejection should mention running job");
}

void testBakeCommandsUseJobService() {
  CommandBus bus;
  IblBakeJobService service;
  registerBakeCommands(bus, service);

  const CommandResult start = bus.dispatch("bake ibl start");
  expect(start.ok, "bake ibl start should succeed");
  expect(contains(start.message, "started bake job 1"),
         "start command should return job id");

  const CommandResult forced = bus.dispatch("bake ibl start --force");
  expect(!forced.ok, "force start while running should fail");
  expect(contains(forced.message, "already running"),
         "force command should explain running job");

  const CommandResult status = bus.dispatch("bake job status 1");
  expect(status.ok, "status command should succeed");
  expect(contains(status.message, "phase="),
         "status command should include phase");
  expect(contains(status.message, "progress="),
         "status command should include progress");

  const CommandResult logs = bus.dispatch("bake job logs 1 0");
  expect(logs.ok, "logs command should succeed");
  expect(contains(logs.message, "[1] info queued"),
         "logs command should include queued event");

  const CommandResult cancel = bus.dispatch("bake job cancel 1");
  expect(cancel.ok, "cancel command should succeed");
  expect(contains(cancel.message, "cancel pending"),
         "cancel command should report cancel pending");
}

} // namespace

int main() {
  testEventSequenceIsMonotonic();
  testRunningJobGuardRejectsForce();
  testBakeCommandsUseJobService();
  return 0;
}
