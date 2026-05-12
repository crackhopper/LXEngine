#pragma once

#include "core/editor/command_bus.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace LX_core {

class EditorState;
class Scene;

struct SceneIoContext {
  using LoadFn = std::function<CommandResult(const std::string &path)>;
  using SaveFn =
      std::function<CommandResult(const std::optional<std::string> &path)>;
  using ListFn = std::function<CommandResult()>;
  using SetAdminFn = std::function<CommandResult(bool enabled)>;
  using AdminStatusFn = std::function<CommandResult()>;
  using CameraControlFn =
      std::function<CommandResult(const std::vector<std::string> &args)>;

  LoadFn load;
  SaveFn save;
  ListFn list;
  SetAdminFn setAdmin;
  AdminStatusFn adminStatus;
  CameraControlFn cameraControl;
};

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene,
                             const SceneIoContext &sceneIoContext = {});

} // namespace LX_core
