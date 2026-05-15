#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/platform/types.hpp"
#include "demos/lxe_editor/camera_rig.hpp"
#include "demos/lxe_editor/editor_config_state.hpp"
#include "demos/lxe_editor/editor_data_state.hpp"
#include "demos/lxe_editor/editor_scene_state.hpp"
#include "demos/lxe_editor/project_session.hpp"
#include "demos/lxe_editor/recording_controller.hpp"
#include "demos/lxe_editor/scene_runtime.hpp"
#include "demos/lxe_editor/ui_overlay.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {
class CommandBus;
class ConsolePanel;
class InspectorPanel;
class SceneTreePanel;
class ViewportOverlay;
} // namespace LX_core

namespace LX_demo::lxe_editor {

class SceneInteractionController;

class LxeEditorSession final {
public:
  struct DisplayCommandHooks final {
    std::function<std::string()> displayListJson;
    std::function<std::string()> displayActiveJson;
    std::function<std::string(std::string_view)> displayConfigGetJson;
    std::function<std::string(std::string_view, std::string_view)>
        displayConfigSet;
    std::function<std::string(std::string_view)> displaySelect;
  };

  LxeEditorSession(CameraRig &rig, UiOverlay &ui,
                   LX_core::EditorState &editorState);
  ~LxeEditorSession();

  void initialize(DisplayCommandHooks displayCommandHooks = {});

  [[nodiscard]] LX_core::SceneSharedPtr scene() const;
  [[nodiscard]] LX_core::CameraComponent &editorCamera() const;
  [[nodiscard]] SceneInteractionController &sceneInteraction() const;
  [[nodiscard]] LX_core::CameraComponent &gameCamera() const;
  [[nodiscard]] bool isDirty() const;
  void setWindowSize(const LX_core::Vec2f &size);
  [[nodiscard]] EditorConfigDocument &editorConfig();
  [[nodiscard]] LX_core::CommandBus &commandBus() const;
  [[nodiscard]] const LX_core::ConsolePanel &consolePanel() const;
  [[nodiscard]] usize bindingsGeneration() const;
  [[nodiscard]] bool debugEnabled() const;
  [[nodiscard]] RecordingController &recording();
  [[nodiscard]] const RecordingController &recording() const;
  [[nodiscard]] std::optional<std::filesystem::path>
  runtimeScenePath() const;
  [[nodiscard]] std::optional<std::string> currentProjectId() const;
  [[nodiscard]] std::optional<std::string> currentProjectDisplayName() const;
  [[nodiscard]] std::optional<std::string> currentProjectActiveScene() const;
  [[nodiscard]] std::optional<std::filesystem::path> currentProjectRoot() const;
  [[nodiscard]] std::optional<std::filesystem::path> activeScenePath() const;
  void persistEditorData();
  void recordCommandHistoryLine(std::string_view line);
  [[nodiscard]] LX_core::CommandResult
  saveScene(const std::optional<std::string> &path);
  void flushPendingSceneOpen(LX_core::gpu::EngineLoop &loop);
  void pollCommandHistory(LX_core::gpu::EngineLoop &loop);

private:
  [[nodiscard]] LX_core::CommandResult
  handleProjectCommand(const std::vector<std::string> &args);
  [[nodiscard]] LX_core::CommandResult
  handleSceneCommand(const std::vector<std::string> &args);
  [[nodiscard]] LX_core::CommandResult queueActiveSceneOpen();
  [[nodiscard]] LX_core::CommandResult saveActiveProjectScene();
  [[nodiscard]] std::string projectSummaryJson() const;
  [[nodiscard]] EditorSceneStateDocument captureEditorSceneState() const;
  void applyEditorSceneState(const EditorSceneStateDocument &state);
  void rebuildBindings(
      std::optional<EditorSceneStateDocument> editorSceneState = std::nullopt);

  CameraRig &m_rig;
  UiOverlay &m_ui;
  LX_core::EditorState &m_editorState;
  ProjectSession m_projectSession;
  SceneRuntime m_runtime;
  std::optional<SceneRuntime> m_pendingRuntime;
  std::optional<std::filesystem::path> m_pendingScenePath;
  std::optional<EditorSceneStateDocument> m_pendingEditorSceneState;
  std::unique_ptr<LX_core::CommandBus> m_commandBus;
  std::unique_ptr<LX_core::ConsolePanel> m_consolePanel;
  std::unique_ptr<LX_core::SceneTreePanel> m_sceneTreePanel;
  std::unique_ptr<LX_core::InspectorPanel> m_inspectorPanel;
  std::unique_ptr<LX_core::ViewportOverlay> m_viewportOverlay;
  std::unique_ptr<SceneInteractionController> m_sceneInteraction;
  size_t m_lastObservedHistoryIndex = 0;
  EditorConfigDocument m_editorConfig;
  EditorDataState m_editorDataState;
  EditorDataDocument m_editorData;
  RecordingController m_recording;
  DisplayCommandHooks m_displayCommandHooks;
  LX_core::Vec2f m_windowSize{1280.0f, 720.0f};
  usize m_bindingsGeneration = 0;
  bool m_debugEnabled = false;
};

} // namespace LX_demo::lxe_editor
