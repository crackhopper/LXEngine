#include "core/scene/ibl_bake_job.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace LX_core {
namespace {

[[nodiscard]] IblBakeStartResult makeStarted(BakeJobId job) {
  return IblBakeStartResult{.ok = true,
                            .job = job,
                            .message = "started bake job " +
                                       std::to_string(job)};
}

[[nodiscard]] IblBakeStartResult makeAlreadyRunning(BakeJobId job) {
  return IblBakeStartResult{.alreadyRunning = true,
                            .job = job,
                            .message = "bake job already running " +
                                       std::to_string(job)};
}

[[nodiscard]] IblBakeStartResult makeRejected(std::string message) {
  return IblBakeStartResult{.rejected = true, .message = std::move(message)};
}

} // namespace

std::string_view iblBakeJobPhaseName(IblBakeJobPhase phase) {
  switch (phase) {
  case IblBakeJobPhase::Queued:
    return "queued";
  case IblBakeJobPhase::CacheCheck:
    return "cache-check";
  case IblBakeJobPhase::Filter:
    return "filter";
  case IblBakeJobPhase::WriteCache:
    return "write-cache";
  case IblBakeJobPhase::ItemComplete:
    return "item-complete";
  case IblBakeJobPhase::Activate:
    return "activate";
  case IblBakeJobPhase::Complete:
    return "complete";
  case IblBakeJobPhase::Failed:
    return "failed";
  case IblBakeJobPhase::ActivationFailed:
    return "activation-failed";
  case IblBakeJobPhase::CancelPending:
    return "cancel-pending";
  }
  return "unknown";
}

std::string_view iblBakeJobSeverityName(IblBakeJobSeverity severity) {
  switch (severity) {
  case IblBakeJobSeverity::Info:
    return "info";
  case IblBakeJobSeverity::Warning:
    return "warning";
  case IblBakeJobSeverity::Error:
    return "error";
  }
  return "unknown";
}

IblBakeJobEvent IblBakeEventQueue::push(IblBakeJobEvent event) {
  std::lock_guard lock(m_mutex);
  event.sequence = m_nextSequence++;
  m_events.push_back(event);
  return event;
}

std::vector<IblBakeJobEvent>
IblBakeEventQueue::drainSince(u64 sequence) const {
  std::lock_guard lock(m_mutex);
  std::vector<IblBakeJobEvent> out;
  for (const IblBakeJobEvent &event : m_events) {
    if (event.sequence > sequence) {
      out.push_back(event);
    }
  }
  return out;
}

IblBakeJobService::~IblBakeJobService() {
  requestWorkerStop();
  if (m_worker.joinable()) {
    m_worker.join();
  }
}

void IblBakeJobService::joinIdleWorker() {
  bool idle = false;
  {
    std::lock_guard lock(m_mutex);
    idle = !m_runningJob.has_value();
  }
  if (idle && m_worker.joinable()) {
    m_worker.join();
  }
}

void IblBakeJobService::requestWorkerStop() {
  std::shared_ptr<RunningJobState> running;
  {
    std::lock_guard lock(m_mutex);
    running = m_runningState;
  }
  if (running) {
    running->cancelRequested = true;
  }
}

IblBakeStartResult IblBakeJobService::start(bool force) {
  joinIdleWorker();

  BakeJobId job = 0;
  std::shared_ptr<RunningJobState> state;
  {
    std::lock_guard lock(m_mutex);
    if (m_runningJob.has_value()) {
      if (force) {
        return makeRejected("bake job already running; cancel it first");
      }
      return makeAlreadyRunning(*m_runningJob);
    }

    job = m_nextJob++;
    state = std::make_shared<RunningJobState>(job);
    m_runningJob = job;
    m_runningState = state;
    m_status[job] = IblBakeJobStatus{
        .job = job,
        .phase = IblBakeJobPhase::Queued,
        .progress = 0.0f,
        .running = true,
        .message = "queued",
    };
  }

  publish(IblBakeJobEvent{
      .job = job,
      .phase = IblBakeJobPhase::Queued,
      .progress = 0.0f,
      .message = "queued",
  });
  publish(IblBakeJobEvent{
      .job = job,
      .phase = IblBakeJobPhase::CacheCheck,
      .progress = 0.05f,
      .message = "cache check",
  });

  m_worker = std::thread(&IblBakeJobService::runWorker, this, state);
  return makeStarted(job);
}

IblBakeCancelResult IblBakeJobService::cancel(BakeJobId job) {
  std::shared_ptr<RunningJobState> state;
  {
    std::lock_guard lock(m_mutex);
    const auto statusIt = m_status.find(job);
    if (statusIt == m_status.end()) {
      return IblBakeCancelResult{
          .notFound = true,
          .job = job,
          .message = "bake job not found " + std::to_string(job),
      };
    }
    if (!m_runningJob.has_value() || *m_runningJob != job ||
        !m_runningState) {
      return IblBakeCancelResult{
          .notFound = true,
          .job = job,
          .message = "bake job not running " + std::to_string(job),
      };
    }
    state = m_runningState;
    statusIt->second.cancelRequested = true;
  }

  state->cancelRequested = true;
  publish(IblBakeJobEvent{
      .job = job,
      .phase = IblBakeJobPhase::CancelPending,
      .progress = 0.05f,
      .message = "cancel pending",
  });
  return IblBakeCancelResult{
      .ok = true,
      .job = job,
      .message = "cancel pending",
  };
}

std::optional<IblBakeJobStatus>
IblBakeJobService::status(BakeJobId job) const {
  std::lock_guard lock(m_mutex);
  const auto it = m_status.find(job);
  if (it == m_status.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<IblBakeJobEvent> IblBakeJobService::logs(BakeJobId job,
                                                     u64 since) const {
  std::vector<IblBakeJobEvent> events = m_events.drainSince(since);
  events.erase(std::remove_if(events.begin(), events.end(),
                              [job](const IblBakeJobEvent &event) {
                                return event.job != job;
                              }),
               events.end());
  return events;
}

void IblBakeJobService::runWorker(std::shared_ptr<RunningJobState> state) {
  while (!state->cancelRequested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::lock_guard lock(m_mutex);
  const auto statusIt = m_status.find(state->job);
  if (statusIt != m_status.end()) {
    statusIt->second.running = false;
    statusIt->second.cancelRequested = true;
  }
  if (m_runningJob.has_value() && *m_runningJob == state->job) {
    m_runningJob.reset();
    m_runningState.reset();
  }
}

IblBakeJobEvent IblBakeJobService::publish(IblBakeJobEvent event) {
  IblBakeJobEvent pushed = m_events.push(std::move(event));
  std::lock_guard lock(m_mutex);
  IblBakeJobStatus &status = m_status[pushed.job];
  status.job = pushed.job;
  status.phase = pushed.phase;
  status.progress = pushed.progress;
  status.lastSequence = pushed.sequence;
  status.message = pushed.message;
  if (pushed.phase == IblBakeJobPhase::CancelPending) {
    status.cancelRequested = true;
  }
  return pushed;
}

} // namespace LX_core
