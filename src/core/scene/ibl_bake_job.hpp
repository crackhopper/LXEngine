#pragma once

#include "core/platform/types.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace LX_core {

using BakeJobId = u64;
using BakeItemId = u64;

enum class IblBakeJobPhase {
  Queued,
  CacheCheck,
  Filter,
  WriteCache,
  ItemComplete,
  Activate,
  Complete,
  Failed,
  ActivationFailed,
  CancelPending,
};

enum class IblBakeJobSeverity {
  Info,
  Warning,
  Error,
};

[[nodiscard]] std::string_view iblBakeJobPhaseName(IblBakeJobPhase phase);
[[nodiscard]] std::string_view
iblBakeJobSeverityName(IblBakeJobSeverity severity);

struct IblBakeJobEvent final {
  BakeJobId job = 0;
  BakeItemId item = 0;
  IblBakeJobPhase phase = IblBakeJobPhase::Queued;
  IblBakeJobSeverity severity = IblBakeJobSeverity::Info;
  float progress = 0.0f;
  std::string message;
  std::string fix;
  u64 sequence = 0;
};

class IblBakeEventQueue final {
public:
  [[nodiscard]] IblBakeJobEvent push(IblBakeJobEvent event);
  [[nodiscard]] std::vector<IblBakeJobEvent> drainSince(u64 sequence) const;

private:
  mutable std::mutex m_mutex;
  u64 m_nextSequence = 1;
  std::vector<IblBakeJobEvent> m_events;
};

struct IblBakeJobStatus final {
  BakeJobId job = 0;
  IblBakeJobPhase phase = IblBakeJobPhase::Queued;
  float progress = 0.0f;
  bool running = false;
  bool cancelRequested = false;
  u64 lastSequence = 0;
  std::string message;
};

struct IblBakeStartResult final {
  bool ok = false;
  bool alreadyRunning = false;
  bool rejected = false;
  BakeJobId job = 0;
  std::string message;
};

struct IblBakeCancelResult final {
  bool ok = false;
  bool notFound = false;
  BakeJobId job = 0;
  std::string message;
};

class IblBakeJobService final {
public:
  IblBakeJobService() = default;
  ~IblBakeJobService();

  IblBakeJobService(const IblBakeJobService &) = delete;
  IblBakeJobService &operator=(const IblBakeJobService &) = delete;

  [[nodiscard]] IblBakeStartResult start(bool force);
  [[nodiscard]] IblBakeCancelResult cancel(BakeJobId job);
  [[nodiscard]] std::optional<IblBakeJobStatus> status(BakeJobId job) const;
  [[nodiscard]] std::vector<IblBakeJobEvent> logs(BakeJobId job,
                                                  u64 since) const;

private:
  struct RunningJobState final {
    explicit RunningJobState(BakeJobId jobId) : job(jobId) {}
    BakeJobId job = 0;
    std::atomic_bool cancelRequested = false;
  };

  void joinIdleWorker();
  void requestWorkerStop();
  void runWorker(std::shared_ptr<RunningJobState> state);
  IblBakeJobEvent publish(IblBakeJobEvent event);

  mutable std::mutex m_mutex;
  IblBakeEventQueue m_events;
  BakeJobId m_nextJob = 1;
  std::optional<BakeJobId> m_runningJob;
  std::shared_ptr<RunningJobState> m_runningState;
  std::unordered_map<BakeJobId, IblBakeJobStatus> m_status;
  std::thread m_worker;
};

} // namespace LX_core
