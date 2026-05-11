#pragma once

// REQ-019: wraps the stock camera controllers behind an explicit editor mode.

#include "core/input/input_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/freefly_camera_controller.hpp"
#include "core/scene/orbit_camera_controller.hpp"

#include <functional>
#include <optional>

namespace LX_demo::lxe_editor {

class CameraRig {
public:
  enum class Mode { Orbit, FreeFly };

  CameraRig();

  // Bind the rig to the camera it will drive. Must be called before update().
  void attach(LX_core::CameraComponent& camera);

  void setMode(Mode mode);

  // Per-frame update: active controller update -> matrix refresh.
  void update(LX_core::IInputState& input, float dt);
  void resyncFromAttachedCamera();

  Mode currentMode() const { return m_mode; }

private:
  std::optional<std::reference_wrapper<LX_core::CameraComponent>> m_camera;
  LX_core::OrbitCameraController m_orbit;
  LX_core::FreeFlyCameraController m_freefly;
  Mode m_mode = Mode::Orbit;
};

} // namespace LX_demo::lxe_editor
