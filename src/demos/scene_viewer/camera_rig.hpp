#pragma once

// REQ-019: wraps the two stock camera controllers with F2 rising-edge mode
// switching. Edge detection is demo-local — Sdl3InputState only exposes
// level state, so the rig remembers the previous frame's F2 down flag.

#include "core/input/input_state.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/freefly_camera_controller.hpp"
#include "core/scene/orbit_camera_controller.hpp"

#include <functional>
#include <optional>

namespace LX_demo::scene_viewer {

class CameraRig {
public:
  enum class Mode { Orbit, FreeFly };

  CameraRig();

  // Bind the rig to the camera it will drive. Must be called before update().
  void attach(LX_core::Camera& camera);

  // Per-frame update: F2 edge detection -> controller update -> matrix refresh.
  void update(LX_core::IInputState& input, float dt);

  Mode currentMode() const { return m_mode; }

private:
  void switchMode();

  std::optional<std::reference_wrapper<LX_core::Camera>> m_camera;
  LX_core::OrbitCameraController m_orbit;
  LX_core::FreeFlyCameraController m_freefly;
  Mode m_mode = Mode::Orbit;
  bool m_prevF2Down = false;
};

} // namespace LX_demo::scene_viewer
