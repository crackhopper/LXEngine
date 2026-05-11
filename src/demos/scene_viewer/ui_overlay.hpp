#pragma once

#include "core/input/input_state.hpp"
#include "core/time/clock.hpp"

#include "editor_config_state.hpp"

namespace LX_core {
class CommandBus;
class ConsolePanel;
class EditorState;
class InspectorPanel;
class SceneTreePanel;
}

#include <functional>
#include <imgui.h>
#include <optional>

namespace LX_demo::scene_viewer {

class CameraRig;

class UiOverlay {
public:
  enum class EditMode { Selection, Orbit, FreeFly };

  void attach(CameraRig& rig, LX_core::CommandBus& commandBus,
              LX_core::EditorState& editorState,
              EditorConfigDocument& editorConfig,
              LX_core::SceneTreePanel& sceneTreePanel,
              LX_core::InspectorPanel& inspectorPanel,
              LX_core::ConsolePanel& consolePanel);
  void attachClock(const LX_core::Clock& clock);

  void drawFrame();
  void handleHotkeys(LX_core::IInputState& input);
  [[nodiscard]] EditMode currentEditMode() const;
  void setEditMode(EditMode mode);
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
  void applyPanelLayout(std::string_view id, const PanelDefaults& defaults);
  void syncPanelLayout(std::string_view id, bool visible);
  void ensurePanelLayout(std::string_view id, const PanelDefaults& defaults,
                         bool visible);
  void syncPanelOpenStatesFromConfig();
  void ensureInitialPanelLayouts();
  void drawToolbarPanel();
  void drawStatsPanel();
  void drawHelpPanel();
  void drawPreferencesPanel();

  std::optional<std::reference_wrapper<const LX_core::Clock>> m_clock;
  std::optional<std::reference_wrapper<CameraRig>> m_rig;
  std::optional<std::reference_wrapper<LX_core::CommandBus>> m_commandBus;
  std::optional<std::reference_wrapper<LX_core::EditorState>> m_editorState;
  std::optional<std::reference_wrapper<EditorConfigDocument>> m_editorConfig;
  std::optional<std::reference_wrapper<LX_core::SceneTreePanel>> m_sceneTreePanel;
  std::optional<std::reference_wrapper<LX_core::InspectorPanel>> m_inspectorPanel;
  std::optional<std::reference_wrapper<LX_core::ConsolePanel>> m_consolePanel;
  bool m_prevF1Down = false;
  bool m_prevFDown = false;
  bool m_prevEscapeDown = false;
  bool m_prevDeleteDown = false;
  bool m_statsVisible = true;
  bool m_helpVisible = true;
  bool m_toolbarVisible = true;
  bool m_preferencesVisible = false;
  bool m_initialLayoutApplied = false;
  bool m_configDirty = false;
  bool m_baseStyleCaptured = false;
  float m_appliedUiFontScale = 1.0f;
  ImGuiStyle m_baseStyle{};
  EditMode m_editMode = EditMode::Orbit;
};

} // namespace LX_demo::scene_viewer
