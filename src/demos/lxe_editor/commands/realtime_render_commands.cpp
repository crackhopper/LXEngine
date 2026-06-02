#include "demos/lxe_editor/commands/register_lxe_editor_commands.hpp"

#include "demos/lxe_editor/commands/lxe_editor_command_helpers.hpp"

namespace LX_demo::lxe_editor {

void registerRealtimeRenderCommands(LX_core::CommandBus &bus,
                                    const LxeEditorCommandContext &context) {
  const auto realtimeRenderListJson = context.realtimeRenderListJson;
  const auto realtimeRenderRun = context.realtimeRenderRun;
  bus.registerHandler(
      "realtime-render", "realtime-render ls | realtime-render run <profile>",
      [realtimeRenderListJson,
       realtimeRenderRun](std::vector<std::string> args) {
        if (args.size() == 1 && args[0] == "ls") {
          if (!realtimeRenderListJson) {
            return makeEditorCommandError("realtime-render ls unavailable");
          }
          const std::string json = realtimeRenderListJson();
          return makeEditorCommandOk("listed realtime render profiles", json);
        }
        if (args.size() == 2 && args[0] == "run") {
          if (!realtimeRenderRun) {
            return makeEditorCommandError("realtime-render run unavailable");
          }
          return realtimeRenderRun(args[1]);
        }
        return makeEditorCommandError(
            "usage: realtime-render ls | realtime-render run <profile>");
      });
}

} // namespace LX_demo::lxe_editor
