#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_events.hpp"
#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"
#include "demos/lxe_editor/recording_controller.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace LX_demo::lxe_editor {

class LxeEditorApiService final {
public:
  struct Hooks final {
    std::function<ApiSceneSummary()> sceneSummary;
    std::function<ApiCameraSnapshot()> cameraSnapshot;
    std::function<ApiToolbarSnapshot()> toolbarSnapshot;
    std::function<std::optional<LX_core::Vec3f>()> lastHitPoint;
    std::function<void(std::string_view)> recordCommandHistoryLine;
    std::function<std::optional<std::reference_wrapper<RecordingController>>()>
        recording;
  };

  LxeEditorApiService(LX_core::CommandBus& commandBus,
                      LX_core::EditorState& editorState,
                      LX_core::Scene& scene, Hooks hooks);

  [[nodiscard]] ApiCommandResponse executeCommand(
      const ApiCommandRequest& request);
  [[nodiscard]] ApiStateSnapshot captureState() const;
  [[nodiscard]] std::string buildInfo() const;
  [[nodiscard]] std::string recordingStatus() const;
  [[nodiscard]] std::string recordingEnable();
  [[nodiscard]] std::string recordingDisable(bool force);
  [[nodiscard]] std::string recordingStart(RecordingDetailLevel detailLevel);
  [[nodiscard]] std::string recordingStop(bool save);
  [[nodiscard]] std::string recordingList() const;
  [[nodiscard]] std::string recordingRead(const std::string& idOrPath) const;
  [[nodiscard]] std::string recordingReplay(const std::string& idOrPath);
  [[nodiscard]] std::string recordingProbe(const std::string& target) const;
  void refresh();

  [[nodiscard]] ApiEventCursor currentCursor() const;
  [[nodiscard]] ApiEventBatch collectEventsSince(
      ApiEventCursor cursor) const;

private:
  static constexpr usize kMaxBufferedEvents = 256;

  [[nodiscard]] ApiSceneSummary captureSceneSummary() const;
  [[nodiscard]] ApiSelectionSnapshot captureSelection() const;
  [[nodiscard]] ApiCameraSnapshot captureCameras() const;
  [[nodiscard]] ApiToolbarSnapshot captureToolbar() const;
  void observeRuntimeSceneEvent(const LX_core::SceneEvent& event);
  void flushPendingRuntimeSceneEvents();
  void observeCommandHistory();
  void observeStateChanges();
  void appendEvent(ApiEvent event);
  [[nodiscard]] static std::string sceneNodeAspectName(
      LX_core::SceneNodeAspect aspect);
  [[nodiscard]] static bool isSceneLoadCommand(std::string_view line);
  [[nodiscard]] static bool isSceneSaveCommand(std::string_view line);

  LX_core::CommandBus& m_commandBus;
  LX_core::EditorState& m_editorState;
  LX_core::Scene& m_scene;
  Hooks m_hooks;
  LX_core::SceneEventSubscription m_sceneSubscription;
  usize m_lastObservedHistoryIndex = 0;
  u64 m_nextSequence = 1;
  ApiStateSnapshot m_lastState;
  std::vector<ApiEvent> m_events;
  std::vector<LX_core::SceneEvent> m_pendingRuntimeSceneEvents;
};

} // namespace LX_demo::lxe_editor
