#include "editor/commands/register_lxe_editor_commands.hpp"

#include "editor/commands/lxe_editor_command_helpers.hpp"

#include <string>
#include <vector>

namespace LX_demo::lxe_editor {
namespace {

void clearUndoRedoOnSuccess(LX_core::CommandResult &result) {
  result.metadata[std::string(
      LX_core::kCommandResultClearUndoOnSuccessMetadataKey)] = "true";
  result.metadata[std::string(
      LX_core::kCommandResultClearRedoOnSuccessMetadataKey)] = "true";
}

void preserveUndoRedoOnSuccess(LX_core::CommandResult &result) {
  result.metadata[std::string(
      LX_core::kCommandResultClearUndoOnSuccessMetadataKey)] = "false";
  result.metadata[std::string(
      LX_core::kCommandResultClearRedoOnSuccessMetadataKey)] = "false";
}

} // namespace

void registerProjectCommands(LX_core::CommandBus &bus,
                             const LxeEditorCommandContext &context) {
  auto projectCommand = context.projectCommand;

  bus.registerHandler("quit", "quit", [](std::vector<std::string> args) {
    if (!args.empty()) {
      return makeEditorCommandError("usage: quit");
    }
    LX_core::CommandResult result =
        makeEditorCommandOk("quitting editor", "{\"quitting\":true}");
    result.metadata["editor.quit"] = "true";
    return result;
  });

  bus.registerHandler(
      "project", "project <args>",
      [projectCommand](std::vector<std::string> args) {
        if (!projectCommand) {
          return makeEditorCommandError("project command unavailable");
        }
        LX_core::CommandResult result = projectCommand(args);
        if (result.ok && !args.empty() &&
            (args[0] == "init" || args[0] == "open" || args[0] == "close")) {
          clearUndoRedoOnSuccess(result);
        } else if (result.ok && !args.empty() && args[0] == "save") {
          preserveUndoRedoOnSuccess(result);
        }
        return result;
      });
}

} // namespace LX_demo::lxe_editor
