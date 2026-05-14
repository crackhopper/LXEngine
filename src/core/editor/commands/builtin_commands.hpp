#pragma once

#include "core/editor/command_bus.hpp"
#include "core/math/vec.hpp"

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
  using GetStringFieldFn =
      std::function<std::optional<std::string>(const std::string &path)>;
  using SetStringFieldFn =
      std::function<CommandResult(const std::string &path,
                                  const std::string &value)>;
  using GetVec3FieldFn =
      std::function<std::optional<Vec3f>(const std::string &path)>;
  using SetVec3FieldFn =
      std::function<CommandResult(const std::string &path, const Vec3f &value)>;
  using ApplyMaterialOverrideFn =
      std::function<CommandResult(const std::string &path,
                                  const std::string &field)>;

  LoadFn load;
  SaveFn save;
  ListFn list;
  SetAdminFn setAdmin;
  AdminStatusFn adminStatus;
  CameraControlFn cameraControl;
  GetStringFieldFn getMaterialUri;
  SetStringFieldFn setMaterialUri;
  GetVec3FieldFn getNodeMaterialBaseColor;
  SetVec3FieldFn setNodeMaterialBaseColor;
  ApplyMaterialOverrideFn applyMaterialOverride;
};

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene,
                             const SceneIoContext &sceneIoContext = {});

} // namespace LX_core
