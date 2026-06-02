#include "demos/lxe_editor/lxe_editor_commands.hpp"

#include "demos/lxe_editor/commands/register_lxe_editor_commands.hpp"

namespace LX_demo::lxe_editor {

void registerLxeEditorCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context) {
  registerProjectCommands(bus, context);
  registerSceneProjectCommands(bus, context);
  registerRecordingCommands(bus, context);
  registerDisplayCommands(bus, context);
  registerRenderDebugCommands(bus, context);
  registerRealtimeRenderCommands(bus, context);
}

} // namespace LX_demo::lxe_editor
