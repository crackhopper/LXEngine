#include "core/task/task_graph.hpp"

#include <unordered_set>

namespace LX_core {

TaskResult TaskResult::success() { return {}; }

TaskResult TaskResult::fatal(std::string diagnostic) {
  TaskResult result;
  result.succeeded = false;
  result.fatalError = true;
  result.diagnostic = std::move(diagnostic);
  return result;
}

TaskContext::TaskContext(TaskId id, std::string phase, std::string name,
                         std::vector<TaskProgressEvent> &events)
    : m_id(id), m_phase(std::move(phase)), m_name(std::move(name)),
      m_events(events) {}

void TaskContext::setProgress(float progress) {
  m_events.push_back(TaskProgressEvent{m_id, m_phase, m_name, progress});
}

TaskId TaskGraph::addTask(TaskSpec spec) {
  const TaskId id = static_cast<TaskId>(m_tasks.size());
  m_tasks.push_back(std::move(spec));
  return id;
}

TaskGraphReport TaskGraph::run() {
  TaskGraphReport report;
  std::unordered_set<TaskId> completed;
  std::unordered_set<TaskId> failed;

  bool madeProgress = true;
  while (completed.size() + failed.size() < m_tasks.size() && madeProgress) {
    madeProgress = false;
    for (TaskId id = 0; id < m_tasks.size(); ++id) {
      if (completed.contains(id) || failed.contains(id)) {
        continue;
      }
      const TaskSpec &task = m_tasks[id];
      bool dependencyFailed = false;
      bool dependenciesReady = true;
      for (TaskId dependency : task.dependencies) {
        if (failed.contains(dependency)) {
          dependencyFailed = true;
          break;
        }
        if (!completed.contains(dependency)) {
          dependenciesReady = false;
          break;
        }
      }
      if (dependencyFailed) {
        failed.insert(id);
        madeProgress = true;
        continue;
      }
      if (!dependenciesReady) {
        continue;
      }
      TaskContext context(id, task.phase, task.name, report.events);
      context.setProgress(0.0f);
      TaskResult result =
          task.run ? task.run(context) : TaskResult::success();
      if (!result.succeeded) {
        report.success = false;
        if (!result.diagnostic.empty()) {
          report.diagnostics.push_back(task.phase + "/" + task.name + ": " +
                                       result.diagnostic);
        }
        failed.insert(id);
        if (result.fatalError) {
          madeProgress = true;
          continue;
        }
      } else {
        context.setProgress(1.0f);
        completed.insert(id);
      }
      madeProgress = true;
    }
  }

  if (completed.size() != m_tasks.size()) {
    report.success = false;
  }
  return report;
}

} // namespace LX_core
