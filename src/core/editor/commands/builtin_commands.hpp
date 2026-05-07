#pragma once

#include "core/editor/command_bus.hpp"

namespace LX_core {

class EditorState;
class Scene;

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene);

} // namespace LX_core
