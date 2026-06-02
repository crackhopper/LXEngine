#include "demos/lxe_editor/commands/register_lxe_editor_commands.hpp"

#include "demos/lxe_editor/commands/lxe_editor_command_helpers.hpp"

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

void registerSceneProjectCommands(LX_core::CommandBus &bus,
                                  const LxeEditorCommandContext &context) {
  auto sceneCommand = context.sceneCommand;

  bus.registerHandler(
      "scene",
      "scene open <name-or-path> | scene import <source-path> <scene-id> | "
      "scene save [args]",
      [sceneCommand](std::vector<std::string> args) {
        if (args.empty()) {
          return makeEditorCommandError(
              "usage: scene open <name-or-path> | scene import <source-path> "
              "<scene-id> | scene save [args]");
        }
        if (args[0] == "load") {
          return makeEditorCommandError(
              "scene command removed; use scene open");
        }
        if (!sceneCommand) {
          return makeEditorCommandError("scene command unavailable");
        }
        LX_core::CommandResult result = sceneCommand(args);
        if ((args[0] == "open" || args[0] == "new" || args[0] == "import" ||
             args[0] == "duplicate") &&
            result.ok) {
          clearUndoRedoOnSuccess(result);
        } else if (args[0] == "save" && result.ok) {
          preserveUndoRedoOnSuccess(result);
        }
        return result;
      });
}

} // namespace LX_demo::lxe_editor
