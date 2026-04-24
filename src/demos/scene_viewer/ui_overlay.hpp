#pragma once

// REQ-019: ImGui overlay for the scene viewer demo. Binds to scene objects
// via non-owning references captured from main(). Hotkey edge detection
// (F1 toggles help) is done locally.

#include "core/input/input_state.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/time/clock.hpp"

#include <functional>
#include <optional>

namespace LX_demo::scene_viewer {

class CameraRig;

class UiOverlay {
public:
  void attach(LX_core::Camera& camera, LX_core::DirectionalLight& light,
              CameraRig& rig);
  void attachClock(const LX_core::Clock& clock);

  // Called from the VulkanRenderer UI callback each frame.
  void drawFrame();

  // Called from the EngineLoop update hook before the rig update.
  void handleHotkeys(LX_core::IInputState& input);

private:
  std::optional<std::reference_wrapper<const LX_core::Clock>> m_clock;
  std::optional<std::reference_wrapper<LX_core::Camera>> m_camera;
  std::optional<std::reference_wrapper<LX_core::DirectionalLight>> m_light;
  std::optional<std::reference_wrapper<CameraRig>> m_rig;
  bool m_prevF1Down = false;
  bool m_helpVisible = true;
};

} // namespace LX_demo::scene_viewer
