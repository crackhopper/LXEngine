#include "editor/commands/lxe_editor_commands.hpp"

#include "core/scene/ibl_bake_service.hpp"
#include "editor/commands/lxe_editor_command_helpers.hpp"
#include "editor/commands/register_lxe_editor_commands.hpp"

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::optional<u64> parseU64(const std::string &text) {
  try {
    size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if (consumed != text.size()) {
      return std::nullopt;
    }
    return static_cast<u64>(value);
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::string
formatBakeJobStatusMessage(const LX_core::IblBakeJobStatus &status) {
  std::ostringstream oss;
  oss << "phase=" << LX_core::iblBakeJobPhaseName(status.phase)
      << " progress=" << std::fixed << std::setprecision(2)
      << status.progress;
  return oss.str();
}

[[nodiscard]] std::string
formatBakeJobLogs(const std::vector<LX_core::IblBakeJobEvent> &events) {
  std::ostringstream oss;
  for (usize i = 0; i < events.size(); ++i) {
    const LX_core::IblBakeJobEvent &event = events[i];
    if (i != 0) {
      oss << '\n';
    }
    oss << '[' << event.sequence << "] "
        << LX_core::iblBakeJobSeverityName(event.severity) << ' '
        << LX_core::iblBakeJobPhaseName(event.phase);
    if (!event.message.empty()) {
      oss << ' ' << event.message;
    }
    if (!event.fix.empty()) {
      oss << " fix=" << event.fix;
    }
  }
  return oss.str();
}

[[nodiscard]] std::string jsonString(std::string_view text) {
  return "\"" + editorCommandJsonEscape(text) + "\"";
}

[[nodiscard]] std::string sequenceRangeJson(
    const std::vector<LX_core::IblBakeJobEvent> &events) {
  if (events.empty()) {
    return "{\"first\":null,\"last\":null}";
  }
  return "{\"first\":" + std::to_string(events.front().sequence) +
         ",\"last\":" + std::to_string(events.back().sequence) + "}";
}

[[nodiscard]] std::string sequenceRangeJson(const u64 sequence) {
  if (sequence == 0u) {
    return "{\"first\":null,\"last\":null}";
  }
  return "{\"first\":" + std::to_string(sequence) +
         ",\"last\":" + std::to_string(sequence) + "}";
}

[[nodiscard]] std::string
latestFixJson(const std::vector<LX_core::IblBakeJobEvent> &events) {
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    if (!it->fix.empty()) {
      return jsonString(it->fix);
    }
  }
  return "null";
}

[[nodiscard]] std::string eventJson(const LX_core::IblBakeJobEvent &event) {
  std::ostringstream oss;
  oss << "{\"sequence\":" << event.sequence << ",\"job\":" << event.job
      << ",\"item\":" << event.item << ",\"phase\":"
      << jsonString(LX_core::iblBakeJobPhaseName(event.phase))
      << ",\"severity\":"
      << jsonString(LX_core::iblBakeJobSeverityName(event.severity))
      << ",\"progress\":" << std::fixed << std::setprecision(2)
      << event.progress << ",\"message\":" << jsonString(event.message)
      << ",\"fix\":";
  if (event.fix.empty()) {
    oss << "null";
  } else {
    oss << jsonString(event.fix);
  }
  oss << "}";
  return oss.str();
}

[[nodiscard]] std::string
eventsJson(const std::vector<LX_core::IblBakeJobEvent> &events) {
  std::ostringstream oss;
  oss << "[";
  for (usize i = 0; i < events.size(); ++i) {
    if (i != 0u) {
      oss << ",";
    }
    oss << eventJson(events[i]);
  }
  oss << "]";
  return oss.str();
}

[[nodiscard]] std::string statusJsonBody(
    const LX_core::IblBakeJobStatus &status,
    const std::vector<LX_core::IblBakeJobEvent> &eventsForFix = {},
    bool includeSequence = true) {
  std::ostringstream oss;
  oss << "\"job\":" << status.job << ",\"phase\":"
      << jsonString(LX_core::iblBakeJobPhaseName(status.phase))
      << ",\"progress\":" << std::fixed << std::setprecision(2)
      << status.progress << ",\"running\":"
      << (status.running ? "true" : "false") << ",\"cancelRequested\":"
      << (status.cancelRequested ? "true" : "false");
  if (includeSequence) {
    oss << ",\"sequence\":" << sequenceRangeJson(status.lastSequence);
  }
  oss << ",\"statusSequence\":" << status.lastSequence;
  oss << ",\"latestFix\":" << latestFixJson(eventsForFix)
      << ",\"message\":" << jsonString(status.message);
  return oss.str();
}

[[nodiscard]] std::string commandStatusJson(
    std::string_view command, const LX_core::IblBakeJobStatus &status,
    const std::vector<LX_core::IblBakeJobEvent> &eventsForFix = {}) {
  return "{\"ok\":true,\"command\":" + jsonString(command) + "," +
         statusJsonBody(status, eventsForFix) + "}";
}

[[nodiscard]] std::string commandEventsJson(
    std::string_view command, const LX_core::IblBakeJobStatus &status,
    const std::vector<LX_core::IblBakeJobEvent> &events, const u64 since = 0) {
  std::ostringstream oss;
  oss << "{\"ok\":true,\"command\":" << jsonString(command) << ","
      << statusJsonBody(status, events, false) << ",\"since\":" << since
      << ",\"sequence\":" << sequenceRangeJson(events)
      << ",\"events\":" << eventsJson(events) << "}";
  return oss.str();
}

[[nodiscard]] std::string commandStatusErrorJson(
    std::string_view command, std::string_view message,
    const LX_core::IblBakeJobStatus &status,
    const std::vector<LX_core::IblBakeJobEvent> &eventsForFix,
    bool alreadyRunning, bool rejected) {
  std::ostringstream structured;
  structured << "{\"ok\":false,\"command\":" << jsonString(command)
             << ",\"error\":" << jsonString(message) << ","
             << statusJsonBody(status, eventsForFix)
             << ",\"alreadyRunning\":"
             << (alreadyRunning ? "true" : "false")
             << ",\"rejected\":" << (rejected ? "true" : "false") << "}";
  return structured.str();
}

[[nodiscard]] LX_core::CommandResult makeBakeCommandError(
    std::string message, std::string_view command, std::string_view usage = {},
    std::optional<LX_core::BakeJobId> job = std::nullopt,
    bool alreadyRunning = false, bool rejected = false) {
  std::ostringstream structured;
  structured << "{\"ok\":false,\"command\":" << jsonString(command)
             << ",\"error\":" << jsonString(message);
  if (!usage.empty()) {
    structured << ",\"usage\":" << jsonString(usage);
  }
  if (job.has_value()) {
    structured << ",\"job\":" << *job;
  }
  structured << ",\"alreadyRunning\":"
             << (alreadyRunning ? "true" : "false")
             << ",\"rejected\":" << (rejected ? "true" : "false") << "}";
  return LX_core::CommandResult{false, std::move(message), structured.str()};
}

[[nodiscard]] LX_core::CommandResult
handleBakeCommand(LX_core::IblBakeJobService &service,
                  const std::vector<std::string> &args) {
  if ((args.size() == 2 || args.size() == 3) && args[0] == "ibl" &&
      args[1] == "start") {
    const bool force = args.size() == 3 && args[2] == "--force";
    if (args.size() == 3 && !force) {
      return makeBakeCommandError("usage: bake ibl start [--force]",
                                  "bake ibl start",
                                  "bake ibl start [--force]");
    }
    const LX_core::IblBakeStartResult started = service.start(force);
    if (!started.ok) {
      if (started.job != 0u) {
        const auto status = service.status(started.job);
        if (status.has_value()) {
          const std::vector<LX_core::IblBakeJobEvent> events =
              service.logs(started.job, 0);
          return LX_core::CommandResult{
              false,
              started.message,
              commandStatusErrorJson("bake ibl start", started.message,
                                     *status, events, started.alreadyRunning,
                                     started.rejected)};
        }
      }
      return makeBakeCommandError(started.message, "bake ibl start",
                                  "bake ibl start [--force]",
                                  started.job == 0u
                                      ? std::nullopt
                                      : std::optional<LX_core::BakeJobId>(
                                            started.job),
                                  started.alreadyRunning, started.rejected);
    }
    const std::vector<LX_core::IblBakeJobEvent> events =
        service.logs(started.job, 0);
    const auto status = service.status(started.job);
    if (!status.has_value()) {
      return makeBakeCommandError("bake job not found " +
                                      std::to_string(started.job),
                                  "bake ibl start", {}, started.job);
    }
    return makeEditorCommandOk(
        started.message,
        commandEventsJson("bake ibl start", *status, events, 0));
  }

  if (args.size() == 2 && args[0] == "job" && args[1] == "status") {
    return makeBakeCommandError("usage: bake job status <id>",
                                "bake job status",
                                "bake job status <id>");
  }
  if (args.size() == 3 && args[0] == "job" && args[1] == "status") {
    const std::optional<u64> job = parseU64(args[2]);
    if (!job.has_value()) {
      return makeBakeCommandError("usage: bake job status <id>",
                                  "bake job status",
                                  "bake job status <id>");
    }
    const auto status = service.status(*job);
    if (!status.has_value()) {
      return makeBakeCommandError("bake job not found " +
                                      std::to_string(*job),
                                  "bake job status", "bake job status <id>",
                                  *job);
    }
    const std::vector<LX_core::IblBakeJobEvent> events =
        service.logs(*job, 0);
    const std::string text = formatBakeJobStatusMessage(*status);
    return makeEditorCommandOk(
        text, commandStatusJson("bake job status", *status, events));
  }

  if (args.size() == 2 && args[0] == "job" && args[1] == "logs") {
    return makeBakeCommandError("usage: bake job logs <id> [since]",
                                "bake job logs",
                                "bake job logs <id> [since]");
  }
  if ((args.size() == 3 || args.size() == 4) && args[0] == "job" &&
      args[1] == "logs") {
    const std::optional<u64> job = parseU64(args[2]);
    if (!job.has_value()) {
      return makeBakeCommandError("usage: bake job logs <id> [since]",
                                  "bake job logs",
                                  "bake job logs <id> [since]");
    }
    u64 since = 0;
    if (args.size() == 4) {
      const std::optional<u64> parsedSince = parseU64(args[3]);
      if (!parsedSince.has_value()) {
        return makeBakeCommandError("usage: bake job logs <id> [since]",
                                    "bake job logs",
                                    "bake job logs <id> [since]");
      }
      since = *parsedSince;
    }
    const auto status = service.status(*job);
    if (!status.has_value()) {
      return makeBakeCommandError("bake job not found " +
                                      std::to_string(*job),
                                  "bake job logs",
                                  "bake job logs <id> [since]", *job);
    }
    const std::vector<LX_core::IblBakeJobEvent> events =
        service.logs(*job, since);
    const std::string text = formatBakeJobLogs(events);
    return makeEditorCommandOk(
        text, commandEventsJson("bake job logs", *status, events, since));
  }

  if (args.size() == 2 && args[0] == "job" && args[1] == "cancel") {
    return makeBakeCommandError("usage: bake job cancel <id>",
                                "bake job cancel",
                                "bake job cancel <id>");
  }
  if (args.size() == 3 && args[0] == "job" && args[1] == "cancel") {
    const std::optional<u64> job = parseU64(args[2]);
    if (!job.has_value()) {
      return makeBakeCommandError("usage: bake job cancel <id>",
                                  "bake job cancel",
                                  "bake job cancel <id>");
    }
    const auto before = service.status(*job);
    const u64 since = before.has_value() ? before->lastSequence : 0u;
    const LX_core::IblBakeCancelResult cancelled = service.cancel(*job);
    if (!cancelled.ok) {
      return makeBakeCommandError(cancelled.message, "bake job cancel",
                                  "bake job cancel <id>", *job);
    }
    const std::vector<LX_core::IblBakeJobEvent> events =
        service.logs(*job, since);
    const auto status = service.status(*job);
    if (!status.has_value()) {
      return makeBakeCommandError("bake job not found " +
                                      std::to_string(*job),
                                  "bake job cancel", "bake job cancel <id>",
                                  *job);
    }
    return makeEditorCommandOk(
        cancelled.message,
        commandEventsJson("bake job cancel", *status, events, since));
  }

  return makeBakeCommandError(
      "usage: bake ibl start [--force] | bake job status <id> | "
      "bake job logs <id> [since] | bake job cancel <id>",
      "bake",
      "bake ibl start [--force] | bake job status <id> | "
      "bake job logs <id> [since] | bake job cancel <id>");
}

} // namespace

void registerBakeCommands(LX_core::CommandBus &bus,
                          LX_core::IblBakeJobService &service) {
  bus.registerHandler(
      "bake",
      "bake ibl start [--force] | bake job status <id> | "
      "bake job logs <id> [since] | bake job cancel <id>",
      [&service](std::vector<std::string> args) {
        return handleBakeCommand(service, args);
      });
}

void registerLxeEditorCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context) {
  registerProjectCommands(bus, context);
  registerSceneProjectCommands(bus, context);
  registerRecordingCommands(bus, context);
  registerDisplayCommands(bus, context);
  registerRenderDebugCommands(bus, context);
  registerRealtimeRenderCommands(bus, context);
  if (context.iblBakeJobs) {
    const auto service = context.iblBakeJobs();
    if (service.has_value()) {
      registerBakeCommands(bus, service->get());
    }
  }
}

} // namespace LX_demo::lxe_editor
