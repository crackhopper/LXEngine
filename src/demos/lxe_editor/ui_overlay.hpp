#pragma once

#include "core/input/input_state.hpp"
#include "core/time/clock.hpp"

#include "editor_config_state.hpp"
#include "recording_controller.hpp"
#include "scene_view_rect.hpp"
#include "selection_camera_input.hpp"

namespace LX_core {
class CommandBus;
class ConsolePanel;
class EditorState;
class InspectorPanel;
class SceneTreePanel;
class ViewportOverlay;
} // namespace LX_core

#include <imgui.h>
#include <functional>
#include <optional>

namespace LX_demo::lxe_editor {

class CameraRig;

class UiOverlay {
public:
  enum class EditorMode { Selection };
  enum class CameraControlMode { Orbit, FreeFly };

  void attach(CameraRig &rig, LX_core::CommandBus &commandBus,
              LX_core::EditorState &editorState,
              EditorConfigDocument &editorConfig,
              LX_core::ViewportOverlay &viewportOverlay,
              LX_core::SceneTreePanel &sceneTreePanel,
              LX_core::InspectorPanel &inspectorPanel,
              LX_core::ConsolePanel &consolePanel,
              std::function<bool()> debugEnabled = {},
              std::function<RecordingStatus()> recordingStatus = {});
  void attachClock(const LX_core::Clock &clock);

  void drawFrame(const LX_core::Vec2f &windowSize);
  void handleHotkeys(LX_core::IInputState &input);
  [[nodiscard]] EditorMode currentEditorMode() const;
  [[nodiscard]] CameraControlMode currentCameraControlMode() const;
  [[nodiscard]] SelectionNavigationMode selectionNavigationMode() const;
  [[nodiscard]] SceneViewRect
  sceneViewRect(const LX_core::Vec2f &windowSize) const;
  [[nodiscard]] bool isGizmoCapturingMouse() const;
  void setEditorMode(EditorMode mode);
  void setCameraControlMode(CameraControlMode mode);
  [[nodiscard]] bool consumeConfigDirty();

private:
  struct PanelDefaults final {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool collapsed = false;
  };

  void applyUiFontScale();
  void applyPanelLayout(std::string_view id, const PanelDefaults &defaults);
  void syncPanelLayout(std::string_view id, bool visible);
  void ensurePanelLayout(std::string_view id, const PanelDefaults &defaults,
                         bool visible);
  void syncPanelOpenStatesFromConfig();
  void ensureInitialPanelLayouts();
  void dispatchCreatePaletteItem(std::string_view kind,
                                 std::string_view displayName);
  void dispatchCreatePaletteDrop(std::string_view kind);
  void drawSceneCreateDropTarget();
  void drawToolbarPanel();
  void drawStatsPanel();
  void drawHelpPanel();
  void drawPreferencesPanel();

  std::optional<std::reference_wrapper<const LX_core::Clock>> m_clock;
  std::optional<std::reference_wrapper<CameraRig>> m_rig;
  std::optional<std::reference_wrapper<LX_core::CommandBus>> m_commandBus;
  std::optional<std::reference_wrapper<LX_core::EditorState>> m_editorState;
  std::optional<std::reference_wrapper<EditorConfigDocument>> m_editorConfig;
  std::optional<std::reference_wrapper<LX_core::ViewportOverlay>>
      m_viewportOverlay;
  std::optional<std::reference_wrapper<LX_core::SceneTreePanel>>
      m_sceneTreePanel;
  std::optional<std::reference_wrapper<LX_core::InspectorPanel>>
      m_inspectorPanel;
  std::optional<std::reference_wrapper<LX_core::ConsolePanel>> m_consolePanel;
  std::function<bool()> m_debugEnabled;
  std::function<RecordingStatus()> m_recordingStatus;
  bool m_prevF1Down = false;
  bool m_prevFDown = false;
  bool m_prevEscapeDown = false;
  bool m_prevDeleteDown = false;
  bool m_prevWDown = false;
  bool m_prevEDown = false;
  bool m_prevRDown = false;
  bool m_statsVisible = true;
  bool m_helpVisible = true;
  bool m_toolbarVisible = true;
  bool m_preferencesVisible = false;
  bool m_initialLayoutApplied = false;
  bool m_configDirty = false;
  bool m_baseStyleCaptured = false;
  float m_appliedUiFontScale = 1.0f;
  ImGuiStyle m_baseStyle{};
  SceneViewRect m_sceneViewRect;
  EditorMode m_editorMode = EditorMode::Selection;
  CameraControlMode m_cameraControlMode = CameraControlMode::Orbit;
  SelectionNavigationMode m_selectionNavigationMode =
      SelectionNavigationMode::Orbit;
};

} // namespace LX_demo::lxe_editor
