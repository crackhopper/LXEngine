#pragma once

#include "core/editor/command_bus.hpp"

#include <functional>
#include <optional>
#include <string>

namespace LX_core {

class EditorState;
class Scene;

struct SceneIoContext {
  using LoadFn = std::function<CommandResult(const std::string &path)>;
  using SaveFn =
      std::function<CommandResult(const std::optional<std::string> &path)>;

  LoadFn load;
  SaveFn save;
};

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene,
                             const SceneIoContext &sceneIoContext = {});

} // namespace LX_core
