#pragma once

#include "editor/commands/lxe_editor_commands.hpp"

namespace LX_demo::lxe_editor {

void registerProjectCommands(LX_core::CommandBus &bus,
                             const LxeEditorCommandContext &context);
void registerSceneProjectCommands(LX_core::CommandBus &bus,
                                  const LxeEditorCommandContext &context);
void registerRecordingCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context);
void registerDisplayCommands(LX_core::CommandBus &bus,
                             const LxeEditorCommandContext &context);
void registerRenderDebugCommands(LX_core::CommandBus &bus,
                                 const LxeEditorCommandContext &context);
void registerRealtimeRenderCommands(LX_core::CommandBus &bus,
                                    const LxeEditorCommandContext &context);

} // namespace LX_demo::lxe_editor
