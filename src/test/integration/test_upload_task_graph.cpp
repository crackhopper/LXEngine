#include "core/task/task_graph.hpp"

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

void testRunsDependencyOrderAndProgress() {
  TaskGraph graph;
  std::vector<std::string> order;
  const TaskId upload = graph.addTask({"upload", "gpu", {}, [&](TaskContext &ctx) {
                                       order.push_back("upload");
                                       ctx.setProgress(1.0f);
                                       return TaskResult::success();
                                     }});
  graph.addTask({"activate", "scene", {upload}, [&](TaskContext &ctx) {
                   order.push_back("activate");
                   ctx.setProgress(1.0f);
                   return TaskResult::success();
                 }});

  const TaskGraphReport report = graph.run();
  EXPECT(report.success, "task graph should succeed");
  EXPECT(order.size() == 2 && order[0] == "upload" && order[1] == "activate",
         "tasks should run in dependency order");
  EXPECT(report.events.size() >= 2, "task graph should report progress events");
}

void testFatalFailureStopsActivation() {
  TaskGraph graph;
  bool activated = false;
  const TaskId upload = graph.addTask({"upload", "gpu", {}, [&](TaskContext &) {
                                       return TaskResult::fatal("upload failed");
                                     }});
  graph.addTask({"activate", "scene", {upload}, [&](TaskContext &) {
                   activated = true;
                   return TaskResult::success();
                 }});

  const TaskGraphReport report = graph.run();
  EXPECT(!report.success, "fatal task should fail graph");
  EXPECT(!activated, "dependent activation task should not run after fatal");
  EXPECT(!report.diagnostics.empty(), "fatal task should emit diagnostics");
}

} // namespace

int main() {
  testRunsDependencyOrderAndProgress();
  testFatalFailureStopsActivation();
  if (g_failures != 0) {
    std::cerr << g_failures << " upload task graph checks failed\n";
    return 1;
  }
  return 0;
}
