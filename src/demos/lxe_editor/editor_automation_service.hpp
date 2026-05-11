#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/editor_automation_protocol.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace LX_demo::lxe_editor {

class EditorAutomationService final {
public:
  struct Hooks final {
    std::function<AutomationSceneSummary()> sceneSummary;
    std::function<AutomationCameraSnapshot()> cameraSnapshot;
    std::function<AutomationToolbarSnapshot()> toolbarSnapshot;
    std::function<std::optional<LX_core::Vec3f>()> lastHitPoint;
    std::function<void(std::string_view)> recordCommandHistoryLine;
  };

  EditorAutomationService(LX_core::CommandBus& commandBus,
                          LX_core::EditorState& editorState,
                          LX_core::Scene& scene, Hooks hooks = {});

  [[nodiscard]] AutomationCommandResponse executeCommand(
      const AutomationCommandRequest& request);
  [[nodiscard]] AutomationStateSnapshot captureState() const;
  void refresh();

  [[nodiscard]] AutomationEventCursor currentCursor() const;
  [[nodiscard]] AutomationEventBatch collectEventsSince(
      AutomationEventCursor cursor) const;

private:
  static constexpr usize kMaxBufferedEvents = 256;

  [[nodiscard]] AutomationSceneSummary captureSceneSummary() const;
  [[nodiscard]] AutomationSelectionSnapshot captureSelection() const;
  [[nodiscard]] AutomationCameraSnapshot captureCameras() const;
  [[nodiscard]] AutomationToolbarSnapshot captureToolbar() const;
  void observeCommandHistory();
  void observeStateChanges();
  void appendEvent(AutomationEvent event);
  [[nodiscard]] static bool isSceneLoadCommand(std::string_view line);
  [[nodiscard]] static bool isSceneSaveCommand(std::string_view line);

  LX_core::CommandBus& m_commandBus;
  LX_core::EditorState& m_editorState;
  LX_core::Scene& m_scene;
  Hooks m_hooks;
  usize m_lastObservedHistoryIndex = 0;
  u64 m_nextSequence = 1;
  AutomationStateSnapshot m_lastState;
  std::vector<AutomationEvent> m_events;
};

} // namespace LX_demo::lxe_editor
