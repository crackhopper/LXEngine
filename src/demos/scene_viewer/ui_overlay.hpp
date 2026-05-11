#pragma once

#include "core/input/input_state.hpp"
#include "core/time/clock.hpp"

namespace LX_core {
class CommandBus;
class ConsolePanel;
class InspectorPanel;
class SceneTreePanel;
class ViewportOverlay;
}

#include <functional>
#include <imgui.h>
#include <optional>

namespace LX_demo::scene_viewer {

class CameraRig;

class UiOverlay {
public:
  void attach(CameraRig& rig, LX_core::CommandBus& commandBus,
              LX_core::SceneTreePanel& sceneTreePanel,
              LX_core::InspectorPanel& inspectorPanel,
              LX_core::ConsolePanel& consolePanel,
              LX_core::ViewportOverlay& viewportOverlay);
  void attachClock(const LX_core::Clock& clock);
  void setDefaultLayoutEnabled(bool enabled);

  void drawFrame();
  void handleHotkeys(LX_core::IInputState& input);

private:
  void applyDefaultLayout();

  std::optional<std::reference_wrapper<const LX_core::Clock>> m_clock;
  std::optional<std::reference_wrapper<CameraRig>> m_rig;
  std::optional<std::reference_wrapper<LX_core::CommandBus>> m_commandBus;
  std::optional<std::reference_wrapper<LX_core::SceneTreePanel>> m_sceneTreePanel;
  std::optional<std::reference_wrapper<LX_core::InspectorPanel>> m_inspectorPanel;
  std::optional<std::reference_wrapper<LX_core::ConsolePanel>> m_consolePanel;
  std::optional<std::reference_wrapper<LX_core::ViewportOverlay>> m_viewportOverlay;
  bool m_prevF1Down = false;
  bool m_prevFDown = false;
  bool m_prevWDown = false;
  bool m_prevEDown = false;
  bool m_prevRDown = false;
  bool m_prevEscapeDown = false;
  bool m_prevDeleteDown = false;
  bool m_helpVisible = true;
  bool m_defaultLayoutEnabled = true;
  bool m_defaultLayoutApplied = false;
  ImVec2 m_lastDisplaySize{0.0f, 0.0f};
};

} // namespace LX_demo::scene_viewer
