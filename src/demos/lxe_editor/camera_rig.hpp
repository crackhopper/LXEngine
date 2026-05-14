#pragma once

// REQ-019: wraps the stock camera controllers behind an explicit editor mode.

#include "core/input/input_state.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/freefly_camera_controller.hpp"
#include "core/scene/orbit_camera_controller.hpp"
#include "demos/lxe_editor/scene_view_rect.hpp"

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
  void handleOrbitTargetControls(const LX_core::IInputState& input,
                                 const LX_core::Scene& scene,
                                 const SceneViewRect& sceneViewRect, float dt);
  void enqueueDebugDraw() const;
  void resyncFromAttachedCamera();
  void setOrbitTarget(const LX_core::Vec3f& target);

  Mode currentMode() const { return m_mode; }
  LX_core::Vec3f orbitTarget() const { return m_orbit.getTarget(); }

private:
  std::optional<std::reference_wrapper<LX_core::CameraComponent>> m_camera;
  LX_core::OrbitCameraController m_orbit;
  LX_core::FreeFlyCameraController m_freefly;
  Mode m_mode = Mode::Orbit;
  bool m_prevOrbitPickKeyDown = false;
};

} // namespace LX_demo::lxe_editor
