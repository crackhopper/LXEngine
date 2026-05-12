#pragma once

#include "core/editor/command_bus.hpp"

#include <functional>
#include <optional>
#include <string>

namespace LX_core {
class EditorState;
class Scene;
}

namespace LX_demo::lxe_editor {

class SceneInteractionController;
struct SceneViewRect;

struct LxeEditorCommandContext final {
  using SceneViewRectFn = std::function<SceneViewRect()>;
  using DirtyFn = std::function<bool()>;
  using DebugEnabledFn = std::function<bool()>;
  using SetDebugEnabledFn = std::function<void(bool)>;
  using GetEditModeFn = std::function<int()>;
  using SetEditModeFn = std::function<void(int)>;
  using PermissionFn = std::function<std::string()>;
  using CurrentDocumentPathFn =
      std::function<std::optional<std::string>()>;
  using CurrentSourceKindFn = std::function<std::optional<std::string>()>;
  using PersistedHistoryFn = std::function<std::vector<std::string>()>;
  using AppendConsoleDebugLineFn = std::function<void(std::string_view)>;

  LX_core::EditorState& editorState;
  LX_core::Scene& scene;
  SceneInteractionController& interaction;
  GetEditModeFn getEditMode;
  SetEditModeFn setEditMode;
  SceneViewRectFn sceneViewRect;
  DirtyFn dirty;
  PermissionFn permission;
  DebugEnabledFn debugEnabled;
  SetDebugEnabledFn setDebugEnabled;
  CurrentDocumentPathFn currentDocumentPath;
  CurrentSourceKindFn currentSourceKind;
  PersistedHistoryFn persistedHistory;
  AppendConsoleDebugLineFn appendConsoleDebugLine;
};

void registerLxeEditorCommands(
    LX_core::CommandBus& bus,
    const LxeEditorCommandContext& context);

} // namespace LX_demo::lxe_editor
