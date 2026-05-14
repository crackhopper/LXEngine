#pragma once

#include "core/editor/command_bus.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LX_core {

class EditorState;
class Scene;
class SceneNode;
using SceneNodeSharedPtr = std::shared_ptr<SceneNode>;

struct SceneIoContext {
  using LoadFn = std::function<CommandResult(const std::string &path)>;
  using SaveFn =
      std::function<CommandResult(const std::optional<std::string> &path)>;
  using ListFn = std::function<CommandResult()>;
  using SetAdminFn = std::function<CommandResult(bool enabled)>;
  using AdminStatusFn = std::function<CommandResult()>;
  using CameraControlFn =
      std::function<CommandResult(const std::vector<std::string> &args)>;
  using CreateNodeFn = std::function<CommandResult(
      const std::string &kind, const std::string &nodeName,
      const std::string &displayName, SceneNodeSharedPtr &outNode)>;

  LoadFn load;
  SaveFn save;
  ListFn list;
  SetAdminFn setAdmin;
  AdminStatusFn adminStatus;
  CameraControlFn cameraControl;
  CreateNodeFn createNode;
};

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene,
                             const SceneIoContext &sceneIoContext = {});

} // namespace LX_core
