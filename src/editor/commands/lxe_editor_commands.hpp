#pragma once

#include "editor/commands/command_bus.hpp"
#include "editor/runtime/recording_controller.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {
class EditorState;
class IblBakeJobService;
class Scene;
} // namespace LX_core

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
  using GetCameraControlModeFn = std::function<int()>;
  using SetCameraControlModeFn = std::function<void(int)>;
  using RuntimeScenePathFn = std::function<std::optional<std::string>()>;
  using ProjectCommandFn =
      std::function<LX_core::CommandResult(const std::vector<std::string> &)>;
  using ProjectSummaryJsonFn = std::function<std::string()>;
  using PersistedHistoryFn = std::function<std::vector<std::string>()>;
  using AppendConsoleDebugLineFn = std::function<void(std::string_view)>;
  using RecordingFn = std::function<
      std::optional<std::reference_wrapper<RecordingController>>()>;
  using BuildInfoJsonFn = std::function<std::string()>;
  using DisplayListJsonFn = std::function<std::string()>;
  using DisplayActiveJsonFn = std::function<std::string()>;
  using DisplayConfigGetJsonFn = std::function<std::string(std::string_view)>;
  using DisplayConfigSetFn =
      std::function<std::string(std::string_view, std::string_view)>;
  using DisplaySelectFn = std::function<std::string(std::string_view)>;
  using DisplayNextFn = std::function<std::string()>;
  using RealtimeRenderListJsonFn = std::function<std::string()>;
  using RealtimeRenderRunFn =
      std::function<LX_core::CommandResult(std::string_view profileName)>;
  using RealtimeRenderModeFn =
      std::function<LX_core::CommandResult(std::string_view modeName)>;
  using IblBakeJobServiceFn = std::function<
      std::optional<std::reference_wrapper<LX_core::IblBakeJobService>>()>;

  LX_core::EditorState &editorState;
  LX_core::Scene &scene;
  SceneInteractionController &interaction;
  GetEditModeFn getEditMode;
  SetEditModeFn setEditMode;
  GetCameraControlModeFn getCameraControlMode;
  SetCameraControlModeFn setCameraControlMode;
  SceneViewRectFn sceneViewRect;
  DirtyFn dirty;
  DebugEnabledFn debugEnabled;
  SetDebugEnabledFn setDebugEnabled;
  RuntimeScenePathFn runtimeScenePath;
  ProjectCommandFn projectCommand;
  ProjectCommandFn sceneCommand;
  ProjectSummaryJsonFn projectSummaryJson;
  PersistedHistoryFn persistedHistory;
  AppendConsoleDebugLineFn appendConsoleDebugLine;
  RecordingFn recording;
  BuildInfoJsonFn buildInfoJson;
  DisplayListJsonFn displayListJson;
  DisplayActiveJsonFn displayActiveJson;
  DisplayConfigGetJsonFn displayConfigGetJson;
  DisplayConfigSetFn displayConfigSet;
  DisplaySelectFn displaySelect;
  DisplayNextFn displayNext;
  RealtimeRenderListJsonFn realtimeRenderListJson;
  RealtimeRenderRunFn realtimeRenderRun;
  RealtimeRenderModeFn realtimeRenderMode;
  IblBakeJobServiceFn iblBakeJobs;
};

void registerBakeCommands(LX_core::CommandBus &bus,
                          LX_core::IblBakeJobService &service);

void registerLxeEditorCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context);

} // namespace LX_demo::lxe_editor
