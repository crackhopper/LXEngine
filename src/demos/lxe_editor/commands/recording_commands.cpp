#include "demos/lxe_editor/commands/register_lxe_editor_commands.hpp"

#include "demos/lxe_editor/commands/lxe_editor_command_helpers.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string
makeRecordingStatusJson(const RecordingStatus &status) {
  std::ostringstream oss;
  oss << "{\"enabled\":" << (status.enabled ? "true" : "false")
      << ",\"active\":" << (status.active ? "true" : "false")
      << ",\"sessionId\":\"" << editorCommandJsonEscape(status.sessionId)
      << "\"" << ",\"detailLevel\":\""
      << recordingDetailLevelName(status.detailLevel) << "\""
      << ",\"stepCount\":" << status.stepCount << ",\"lastSavedPath\":";
  if (status.lastSavedPath.has_value()) {
    oss << '"' << editorCommandJsonEscape(status.lastSavedPath->string())
        << '"';
  } else {
    oss << "null";
  }
  oss << "}";
  return oss.str();
}

[[nodiscard]] std::optional<RecordingDetailLevel>
parseRecordingDetailLevel(const std::vector<std::string> &args,
                          const usize index) {
  if (index >= args.size()) {
    return RecordingDetailLevel::Basic;
  }
  return recordingDetailLevelFromName(args[index]);
}

} // namespace

void registerRecordingCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context) {
  auto runtimeScenePath = context.runtimeScenePath;
  auto recording = context.recording;
  auto buildInfoJson = context.buildInfoJson;

  bus.registerHandler(
      "recording",
      "recording status|enable|disable [force]|start "
      "[basic|diagnostic|trace]|stop [save|discard]",
      [recording, runtimeScenePath,
       buildInfoJson](std::vector<std::string> args) {
        if (!recording) {
          return makeEditorCommandError("recording unavailable");
        }
        const auto recorder = recording();
        if (!recorder.has_value()) {
          return makeEditorCommandError("recording unavailable");
        }
        RecordingController &controller = recorder->get();
        if (args.empty() || args[0] == "status") {
          const std::string structured =
              makeRecordingStatusJson(controller.status());
          return makeEditorCommandOk(structured, structured);
        }
        if (args[0] == "enable") {
          if (args.size() != 1) {
            return makeEditorCommandError("usage: recording enable");
          }
          controller.enable();
          return makeEditorCommandOk(
              "recording enabled",
              makeRecordingStatusJson(controller.status()));
        }
        if (args[0] == "disable") {
          if (args.size() > 2 || (args.size() == 2 && args[1] != "force")) {
            return makeEditorCommandError("usage: recording disable [force]");
          }
          if (!controller.disable(args.size() == 2)) {
            return makeEditorCommandError(
                "recording active; use recording disable force");
          }
          return makeEditorCommandOk(
              "recording disabled",
              makeRecordingStatusJson(controller.status()));
        }
        if (args[0] == "start") {
          if (args.size() > 2) {
            return makeEditorCommandError(
                "usage: recording start [basic|diagnostic|trace]");
          }
          const auto detailLevel = parseRecordingDetailLevel(args, 1);
          if (!detailLevel.has_value()) {
            return makeEditorCommandError(
                "usage: recording start [basic|diagnostic|trace]");
          }
          const auto path =
              runtimeScenePath ? runtimeScenePath() : std::nullopt;
          const auto result = controller.start(RecordingStartOptions{
              .detailLevel = *detailLevel,
              .scenePath = path.value_or(std::string{}),
              .buildInfoJson =
                  buildInfoJson ? buildInfoJson() : std::string{"{}"},
          });
          return makeEditorCommandOk(
              "recording started",
              "{\"active\":" + std::string(result.active ? "true" : "false") +
                  ",\"sessionId\":\"" +
                  editorCommandJsonEscape(result.sessionId) + "\"}");
        }
        if (args[0] == "stop") {
          if (args.size() > 2 ||
              (args.size() == 2 && args[1] != "save" && args[1] != "discard")) {
            return makeEditorCommandError(
                "usage: recording stop [save|discard]");
          }
          const bool save = args.size() < 2 || args[1] == "save";
          const auto result =
              controller.stop(RecordingStopOptions{.save = save});
          std::ostringstream oss;
          oss << "{\"saved\":" << (result.saved ? "true" : "false")
              << ",\"path\":\"" << editorCommandJsonEscape(result.path.string())
              << "\"" << ",\"stepCount\":" << result.stepCount
              << ",\"sessionId\":\""
              << editorCommandJsonEscape(result.sessionId) << "\"}";
          return makeEditorCommandOk(result.saved ? "recording saved"
                                                  : "recording stopped",
                                     oss.str());
        }
        return makeEditorCommandError(
            "usage: recording status|enable|disable [force]|start "
            "[basic|diagnostic|trace]|stop [save|discard]");
      });

  bus.registerCompleter("recording", 0,
                        [](const LX_core::CompletionContext &context) {
                          static const std::vector<std::string> kActions = {
                              "status", "enable", "disable", "start", "stop"};
                          std::vector<std::string> out;
                          for (const auto &action : kActions) {
                            if (action.rfind(context.partialToken, 0) == 0) {
                              out.push_back(action);
                            }
                          }
                          return out;
                        });
}

} // namespace LX_demo::lxe_editor
