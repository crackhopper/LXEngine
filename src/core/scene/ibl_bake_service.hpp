#pragma once

#include "core/frame_graph/frame_graph_executor.hpp"
#include "core/scene/ibl_bake_job.hpp"
#include "core/scene/ibl_bake_keys.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LX_core {

enum class IblBakeCacheState {
  Hit,
  Missing,
  Invalid,
};

struct IblBakeCacheCheckResult final {
  IblBakeCacheState state = IblBakeCacheState::Missing;
  std::string reason;

  [[nodiscard]] static IblBakeCacheCheckResult hit(std::string reason = {});
  [[nodiscard]] static IblBakeCacheCheckResult missing(std::string reason);
  [[nodiscard]] static IblBakeCacheCheckResult invalid(std::string reason);
};

struct IblBakeCacheWriteResult final {
  bool ok = false;
  std::vector<std::string> diagnostics;

  [[nodiscard]] static IblBakeCacheWriteResult success();
  [[nodiscard]] static IblBakeCacheWriteResult failure(std::string diagnostic);
};

class IblBakeCacheStore {
public:
  virtual ~IblBakeCacheStore() = default;

  [[nodiscard]] virtual IblBakeCacheCheckResult
  check(const IblBakeItem &item) = 0;
  [[nodiscard]] virtual IblBakeCacheWriteResult
  write(const IblBakeItem &item,
        const FrameGraphExecutionResult &execution) = 0;
};

struct IblBakeActivationResult final {
  bool ok = true;
  std::string message;

  [[nodiscard]] static IblBakeActivationResult success(std::string message = {});
  [[nodiscard]] static IblBakeActivationResult failure(std::string message);
};

class IblBakeActivationSink {
public:
  virtual ~IblBakeActivationSink() = default;

  [[nodiscard]] virtual IblBakeActivationResult
  activate(std::span<const IblBakeItem> items) = 0;
};

class IblBakeActivationDispatcher {
public:
  virtual ~IblBakeActivationDispatcher() = default;

  [[nodiscard]] virtual IblBakeActivationResult
  dispatchActivation(IblBakeActivationSink &sink,
                     std::span<const IblBakeItem> items) = 0;
};

struct IblBakeJobServiceConfig final {
  std::vector<IblBakeItem> items;
  std::shared_ptr<IblBakeCacheStore> cacheStore;
  std::shared_ptr<FrameGraphExecutor> executor;
  std::shared_ptr<IblBakeActivationSink> activation;
  std::shared_ptr<IblBakeActivationDispatcher> activationDispatcher;
  std::function<FrameGraphExecutionRequest(const IblBakeItem &item)>
      makeExecutionRequest;
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
  explicit IblBakeJobService(IblBakeJobServiceConfig config);
  ~IblBakeJobService();

  IblBakeJobService(const IblBakeJobService &) = delete;
  IblBakeJobService &operator=(const IblBakeJobService &) = delete;

  void configure(IblBakeJobServiceConfig config);

  [[nodiscard]] IblBakeStartResult start(bool force);
  [[nodiscard]] IblBakeStartResult startBake(bool force = false);
  [[nodiscard]] IblBakeCancelResult cancel(BakeJobId job);
  [[nodiscard]] std::optional<IblBakeJobStatus> status(BakeJobId job) const;
  [[nodiscard]] std::vector<IblBakeJobEvent> events(u64 since) const;
  [[nodiscard]] std::vector<IblBakeJobEvent> logs(BakeJobId job,
                                                  u64 since) const;

private:
  struct RunningJobState final {
    RunningJobState(BakeJobId jobId, bool forceBake,
                    IblBakeJobServiceConfig jobConfig)
        : job(jobId), force(forceBake), config(std::move(jobConfig)) {}

    BakeJobId job = 0;
    bool force = false;
    IblBakeJobServiceConfig config;
    std::atomic_bool cancelRequested = false;
  };

  [[nodiscard]] bool joinCompletedWorker();
  void requestWorkerStop();
  void runWorker(std::shared_ptr<RunningJobState> state);
  void runBakeJob(const std::shared_ptr<RunningJobState> &state);
  [[nodiscard]] bool cancelObserved(
      const std::shared_ptr<RunningJobState> &state);
  void finishRunningJob(const std::shared_ptr<RunningJobState> &state);
  IblBakeJobEvent publish(IblBakeJobEvent event);

  mutable std::mutex m_mutex;
  IblBakeEventQueue m_events;
  BakeJobId m_nextJob = 1;
  IblBakeJobServiceConfig m_config;
  std::optional<BakeJobId> m_runningJob;
  std::shared_ptr<RunningJobState> m_runningState;
  std::unordered_map<BakeJobId, IblBakeJobStatus> m_status;
  std::thread m_worker;
};

} // namespace LX_core
