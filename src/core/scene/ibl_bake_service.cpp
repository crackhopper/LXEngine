#include "core/scene/ibl_bake_service.hpp"

#include <algorithm>
#include <exception>
#include <sstream>
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

[[nodiscard]] std::string joinDiagnostics(
    const std::vector<std::string> &diagnostics) {
  std::ostringstream oss;
  for (usize i = 0; i < diagnostics.size(); ++i) {
    if (i != 0u) {
      oss << "; ";
    }
    oss << diagnostics[i];
  }
  return oss.str();
}

[[nodiscard]] FrameGraphExecutionRequest
makeDefaultExecutionRequest(const IblBakeItem &) {
  return {};
}

} // namespace

IblBakeCacheCheckResult IblBakeCacheCheckResult::hit(std::string reason) {
  return IblBakeCacheCheckResult{.state = IblBakeCacheState::Hit,
                                 .reason = std::move(reason)};
}

IblBakeCacheCheckResult
IblBakeCacheCheckResult::missing(std::string reason) {
  return IblBakeCacheCheckResult{.state = IblBakeCacheState::Missing,
                                 .reason = std::move(reason)};
}

IblBakeCacheCheckResult
IblBakeCacheCheckResult::invalid(std::string reason) {
  return IblBakeCacheCheckResult{.state = IblBakeCacheState::Invalid,
                                 .reason = std::move(reason)};
}

IblBakeCacheWriteResult IblBakeCacheWriteResult::success() {
  return IblBakeCacheWriteResult{.ok = true};
}

IblBakeCacheWriteResult
IblBakeCacheWriteResult::failure(std::string diagnostic) {
  return IblBakeCacheWriteResult{.diagnostics = {std::move(diagnostic)}};
}

IblBakeActivationResult
IblBakeActivationResult::success(std::string message) {
  return IblBakeActivationResult{.ok = true, .message = std::move(message)};
}

IblBakeActivationResult
IblBakeActivationResult::failure(std::string message) {
  return IblBakeActivationResult{.ok = false, .message = std::move(message)};
}

IblBakeJobService::IblBakeJobService(IblBakeJobServiceConfig config)
    : m_config(std::move(config)) {}

IblBakeJobService::~IblBakeJobService() {
  requestWorkerStop();
  if (m_worker.joinable()) {
    m_worker.join();
  }
}

void IblBakeJobService::configure(IblBakeJobServiceConfig config) {
  while (joinCompletedWorker()) {
  }
  std::lock_guard lock(m_mutex);
  if (m_runningJob.has_value()) {
    return;
  }
  m_config = std::move(config);
}

bool IblBakeJobService::joinCompletedWorker() {
  std::thread completedWorker;
  {
    std::lock_guard lock(m_mutex);
    if (m_runningJob.has_value() || !m_worker.joinable()) {
      return false;
    }
    completedWorker = std::move(m_worker);
  }
  completedWorker.join();
  return true;
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
  return startBake(force);
}

IblBakeStartResult IblBakeJobService::startBake(bool force) {
  BakeJobId job = 0;
  std::shared_ptr<RunningJobState> state;
  while (!state) {
    std::thread completedWorker;
    std::unique_lock lock(m_mutex);
    if (m_runningJob.has_value()) {
      if (force) {
        return makeRejected("bake job already running; cancel it first");
      }
      return makeAlreadyRunning(*m_runningJob);
    }
    if (m_worker.joinable()) {
      completedWorker = std::move(m_worker);
      lock.unlock();
      completedWorker.join();
      continue;
    }

    job = m_nextJob++;
    state = std::make_shared<RunningJobState>(job, force, m_config);
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

  {
    std::lock_guard lock(m_mutex);
    m_worker = std::thread(&IblBakeJobService::runWorker, this, state);
  }
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
  try {
    runBakeJob(state);
  } catch (const std::exception &error) {
    publish(IblBakeJobEvent{
        .job = state->job,
        .phase = IblBakeJobPhase::Failed,
        .severity = IblBakeJobSeverity::Error,
        .progress = 1.0f,
        .message = std::string{"bake job failed: "} + error.what(),
    });
  }
  finishRunningJob(state);
}

void IblBakeJobService::runBakeJob(
    const std::shared_ptr<RunningJobState> &state) {
  publish(IblBakeJobEvent{
      .job = state->job,
      .phase = IblBakeJobPhase::CacheCheck,
      .progress = 0.05f,
      .message = "cache check",
  });
  if (cancelObserved(state)) {
    return;
  }

  const IblBakeJobServiceConfig &config = state->config;
  if (config.items.empty()) {
    publish(IblBakeJobEvent{
        .job = state->job,
        .phase = IblBakeJobPhase::Filter,
        .progress = 0.25f,
        .message = "no bake items",
    });
    if (cancelObserved(state)) {
      return;
    }
    publish(IblBakeJobEvent{
        .job = state->job,
        .phase = IblBakeJobPhase::Activate,
        .progress = 0.90f,
        .message = "no bake items to activate",
    });
    publish(IblBakeJobEvent{
        .job = state->job,
        .phase = IblBakeJobPhase::Complete,
        .progress = 1.0f,
        .message = "complete",
    });
    return;
  }

  std::vector<IblBakeItem> activationItems;
  activationItems.reserve(config.items.size());
  const auto requestFactory =
      config.makeExecutionRequest ? config.makeExecutionRequest
                                  : makeDefaultExecutionRequest;

  for (const IblBakeItem &item : config.items) {
    if (cancelObserved(state)) {
      return;
    }

    bool requiresBake = state->force;
    if (state->force) {
      publish(IblBakeJobEvent{
          .job = state->job,
          .item = item.id,
          .phase = IblBakeJobPhase::CacheCheck,
          .progress = 0.10f,
          .message = "force rebake ignores valid cache",
      });
    } else if (config.cacheStore != nullptr) {
      const IblBakeCacheCheckResult cache = config.cacheStore->check(item);
      if (cache.state == IblBakeCacheState::Hit) {
        publish(IblBakeJobEvent{
            .job = state->job,
            .item = item.id,
            .phase = IblBakeJobPhase::CacheCheck,
            .progress = 0.20f,
            .message = cache.reason.empty() ? "cache hit"
                                            : "cache hit: " + cache.reason,
        });
        activationItems.push_back(item);
        continue;
      }
      requiresBake = true;
      publish(IblBakeJobEvent{
          .job = state->job,
          .item = item.id,
          .phase = IblBakeJobPhase::CacheCheck,
          .severity = cache.state == IblBakeCacheState::Invalid
                          ? IblBakeJobSeverity::Warning
                          : IblBakeJobSeverity::Info,
          .progress = 0.20f,
          .message = cache.state == IblBakeCacheState::Invalid
                         ? "invalid cache: " + cache.reason
                         : "cache miss: " + cache.reason,
      });
    } else {
      requiresBake = true;
      publish(IblBakeJobEvent{
          .job = state->job,
          .item = item.id,
          .phase = IblBakeJobPhase::CacheCheck,
          .message = "cache store unavailable",
      });
    }

    if (!requiresBake) {
      activationItems.push_back(item);
      continue;
    }
    if (cancelObserved(state)) {
      return;
    }

    publish(IblBakeJobEvent{
        .job = state->job,
        .item = item.id,
        .phase = IblBakeJobPhase::Filter,
        .progress = 0.35f,
        .message = "prepare graph bake work",
    });
    if (config.executor == nullptr) {
      publish(IblBakeJobEvent{
          .job = state->job,
          .item = item.id,
          .phase = IblBakeJobPhase::Failed,
          .severity = IblBakeJobSeverity::Error,
          .progress = 1.0f,
          .message = "bake executor is required",
      });
      return;
    }

    const FrameGraphExecutionResult execution =
        config.executor->execute(requestFactory(item));
    if (!execution.ok) {
      publish(IblBakeJobEvent{
          .job = state->job,
          .item = item.id,
          .phase = IblBakeJobPhase::Failed,
          .severity = IblBakeJobSeverity::Error,
          .progress = 1.0f,
          .message = execution.diagnostics.empty()
                         ? "frame graph bake failed"
                         : "frame graph bake failed: " +
                               joinDiagnostics(execution.diagnostics),
      });
      return;
    }
    if (cancelObserved(state)) {
      return;
    }

    publish(IblBakeJobEvent{
        .job = state->job,
        .item = item.id,
        .phase = IblBakeJobPhase::WriteCache,
        .progress = 0.70f,
        .message = "write cache",
    });
    if (config.cacheStore != nullptr) {
      const IblBakeCacheWriteResult written =
          config.cacheStore->write(item, execution);
      if (!written.ok) {
        publish(IblBakeJobEvent{
            .job = state->job,
            .item = item.id,
            .phase = IblBakeJobPhase::Failed,
            .severity = IblBakeJobSeverity::Error,
            .progress = 1.0f,
            .message = written.diagnostics.empty()
                           ? "cache write failed"
                           : "cache write failed: " +
                                 joinDiagnostics(written.diagnostics),
        });
        return;
      }
    }

    publish(IblBakeJobEvent{
        .job = state->job,
        .item = item.id,
        .phase = IblBakeJobPhase::ItemComplete,
        .progress = 0.80f,
        .message = "item complete",
    });
    activationItems.push_back(item);
  }

  if (cancelObserved(state)) {
    return;
  }

  publish(IblBakeJobEvent{
      .job = state->job,
      .phase = IblBakeJobPhase::Activate,
      .progress = 0.90f,
      .message = "activate",
  });
  if (config.activation != nullptr) {
    if (config.activationDispatcher == nullptr) {
      publish(IblBakeJobEvent{
          .job = state->job,
          .phase = IblBakeJobPhase::ActivationFailed,
          .severity = IblBakeJobSeverity::Error,
          .progress = 1.0f,
          .message = "activation dispatcher is required",
      });
      return;
    }
    const IblBakeActivationResult activated =
        config.activationDispatcher->dispatchActivation(*config.activation,
                                                        activationItems);
    if (!activated.ok) {
      publish(IblBakeJobEvent{
          .job = state->job,
          .phase = IblBakeJobPhase::ActivationFailed,
          .severity = IblBakeJobSeverity::Error,
          .progress = 1.0f,
          .message = activated.message.empty() ? "activation failed"
                                               : activated.message,
      });
      return;
    }
  }

  publish(IblBakeJobEvent{
      .job = state->job,
      .phase = IblBakeJobPhase::Complete,
      .progress = 1.0f,
      .message = "complete",
  });
}

bool IblBakeJobService::cancelObserved(
    const std::shared_ptr<RunningJobState> &state) {
  if (!state->cancelRequested.load()) {
    return false;
  }
  publish(IblBakeJobEvent{
      .job = state->job,
      .phase = IblBakeJobPhase::CancelPending,
      .progress = 1.0f,
      .message = "cancelled",
  });
  return true;
}

void IblBakeJobService::finishRunningJob(
    const std::shared_ptr<RunningJobState> &state) {
  std::lock_guard lock(m_mutex);
  const auto statusIt = m_status.find(state->job);
  if (statusIt != m_status.end()) {
    statusIt->second.running = false;
    if (state->cancelRequested.load()) {
      statusIt->second.cancelRequested = true;
    }
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
