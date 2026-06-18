#include "editor/commands/lxe_editor_commands.hpp"

#include "core/scene/ibl_bake_job.hpp"
#include "editor/commands/lxe_editor_command_helpers.hpp"
#include "editor/commands/register_lxe_editor_commands.hpp"

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
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
formatBakeJobStatus(const LX_core::IblBakeJobStatus &status) {
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

[[nodiscard]] LX_core::CommandResult
handleBakeCommand(LX_core::IblBakeJobService &service,
                  const std::vector<std::string> &args) {
  if ((args.size() == 2 || args.size() == 3) && args[0] == "ibl" &&
      args[1] == "start") {
    const bool force = args.size() == 3 && args[2] == "--force";
    if (args.size() == 3 && !force) {
      return makeEditorCommandError("usage: bake ibl start [--force]");
    }
    const LX_core::IblBakeStartResult started = service.start(force);
    if (!started.ok) {
      return makeEditorCommandError(started.message);
    }
    return makeEditorCommandOk(started.message);
  }

  if (args.size() == 3 && args[0] == "job" && args[1] == "status") {
    const std::optional<u64> job = parseU64(args[2]);
    if (!job.has_value()) {
      return makeEditorCommandError("usage: bake job status <id>");
    }
    const auto status = service.status(*job);
    if (!status.has_value()) {
      return makeEditorCommandError("bake job not found " +
                                    std::to_string(*job));
    }
    const std::string text = formatBakeJobStatus(*status);
    return makeEditorCommandOk(text, text);
  }

  if ((args.size() == 3 || args.size() == 4) && args[0] == "job" &&
      args[1] == "logs") {
    const std::optional<u64> job = parseU64(args[2]);
    if (!job.has_value()) {
      return makeEditorCommandError("usage: bake job logs <id> [since]");
    }
    u64 since = 0;
    if (args.size() == 4) {
      const std::optional<u64> parsedSince = parseU64(args[3]);
      if (!parsedSince.has_value()) {
        return makeEditorCommandError("usage: bake job logs <id> [since]");
      }
      since = *parsedSince;
    }
    if (!service.status(*job).has_value()) {
      return makeEditorCommandError("bake job not found " +
                                    std::to_string(*job));
    }
    const std::string text = formatBakeJobLogs(service.logs(*job, since));
    return makeEditorCommandOk(text, text);
  }

  if (args.size() == 3 && args[0] == "job" && args[1] == "cancel") {
    const std::optional<u64> job = parseU64(args[2]);
    if (!job.has_value()) {
      return makeEditorCommandError("usage: bake job cancel <id>");
    }
    const LX_core::IblBakeCancelResult cancelled = service.cancel(*job);
    if (!cancelled.ok) {
      return makeEditorCommandError(cancelled.message);
    }
    return makeEditorCommandOk(cancelled.message);
  }

  return makeEditorCommandError(
      "usage: bake ibl start [--force] | bake job status <id> | "
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
