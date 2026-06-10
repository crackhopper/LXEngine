#pragma once

#include "core/platform/types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace LX_core {

using TaskId = u32;

struct TaskProgressEvent final {
  TaskId id = 0;
  std::string phase;
  std::string name;
  float progress = 0.0f;
};

struct TaskResult final {
  bool succeeded = true;
  bool fatalError = false;
  std::string diagnostic;

  static TaskResult success();
  static TaskResult fatal(std::string diagnostic);
};

class TaskContext final {
public:
  TaskContext(TaskId id, std::string phase, std::string name,
              std::vector<TaskProgressEvent> &events);

  void setProgress(float progress);

private:
  TaskId m_id;
  std::string m_phase;
  std::string m_name;
  std::vector<TaskProgressEvent> &m_events;
};

struct TaskSpec final {
  std::string name;
  std::string phase;
  std::vector<TaskId> dependencies;
  std::function<TaskResult(TaskContext &)> run;
};

struct TaskGraphReport final {
  bool success = true;
  std::vector<TaskProgressEvent> events;
  std::vector<std::string> diagnostics;
};

class TaskGraph final {
public:
  [[nodiscard]] TaskId addTask(TaskSpec spec);
  [[nodiscard]] TaskGraphReport run();

private:
  std::vector<TaskSpec> m_tasks;
};

} // namespace LX_core
