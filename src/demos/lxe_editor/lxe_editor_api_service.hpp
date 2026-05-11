#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"

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
  };

  LxeEditorApiService(LX_core::CommandBus& commandBus,
                      LX_core::EditorState& editorState,
                      LX_core::Scene& scene, Hooks hooks = {});

  [[nodiscard]] ApiCommandResponse executeCommand(
      const ApiCommandRequest& request);
  [[nodiscard]] ApiStateSnapshot captureState() const;
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
  void observeCommandHistory();
  void observeStateChanges();
  void appendEvent(ApiEvent event);
  [[nodiscard]] static bool isSceneLoadCommand(std::string_view line);
  [[nodiscard]] static bool isSceneSaveCommand(std::string_view line);

  LX_core::CommandBus& m_commandBus;
  LX_core::EditorState& m_editorState;
  LX_core::Scene& m_scene;
  Hooks m_hooks;
  usize m_lastObservedHistoryIndex = 0;
  u64 m_nextSequence = 1;
  ApiStateSnapshot m_lastState;
  std::vector<ApiEvent> m_events;
};

} // namespace LX_demo::lxe_editor
