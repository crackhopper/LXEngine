#include "camera_rig.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace LX_demo::lxe_editor {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float clampUnit(float v) {
  if (v < -1.0f) return -1.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

// Reconstruct orbit state (target + distance + yaw/pitch) from a free camera
// pose so flipping to orbit mode keeps the viewed point stable.
void syncOrbitFromCamera(LX_core::OrbitCameraController& ctrl,
                         const LX_core::CameraComponent& cam) {
  const LX_core::Vec3f eye = cam.getEyePosition();
  const LX_core::Vec3f target = cam.getLookTarget();
  const LX_core::Vec3f offset = eye - target;
  const float distance = offset.length();
  ctrl.setTarget(target);
  if (distance > 1e-6f) {
    ctrl.setDistance(distance);
    ctrl.setYawDeg(std::atan2(offset.x, offset.z) * 180.0f / kPi);
    ctrl.setPitchDeg(std::asin(clampUnit(offset.y / distance)) * 180.0f / kPi);
  }
}

// Reconstruct freefly state (position + yaw/pitch) from the current camera
// pose so the switch keeps the framing intact.
void syncFreeFlyFromCamera(LX_core::FreeFlyCameraController& ctrl,
                           const LX_core::CameraComponent& cam) {
  const LX_core::Vec3f forward = cam.getForwardVector();
  ctrl.setPosition(cam.getEyePosition());
  ctrl.setYawDeg(std::atan2(forward.x, forward.z) * 180.0f / kPi);
  ctrl.setPitchDeg(std::asin(clampUnit(forward.y)) * 180.0f / kPi);
}

} // namespace

CameraRig::CameraRig()
    : m_orbit(LX_core::Vec3f{0.0f, 0.0f, 0.0f}, 3.0f, 0.0f, 0.0f),
      m_freefly(LX_core::Vec3f{0.0f, 0.0f, 3.0f}, 180.0f, 0.0f) {}

void CameraRig::attach(LX_core::CameraComponent& camera) {
  m_camera = std::ref(camera);
  syncOrbitFromCamera(m_orbit, camera);
  syncFreeFlyFromCamera(m_freefly, camera);
}

void CameraRig::setMode(const Mode mode) {
  if (m_mode == mode) {
    return;
  }
  if (!m_camera) return;
  if (mode == Mode::FreeFly) {
    syncFreeFlyFromCamera(m_freefly, m_camera->get());
  } else {
    syncOrbitFromCamera(m_orbit, m_camera->get());
  }
  m_mode = mode;
  std::cerr << "[lxe_editor] camera mode -> "
            << (m_mode == Mode::Orbit ? "Orbit" : "FreeFly") << "\n";
}

void CameraRig::resyncFromAttachedCamera() {
  if (!m_camera) {
    return;
  }
  syncOrbitFromCamera(m_orbit, m_camera->get());
  syncFreeFlyFromCamera(m_freefly, m_camera->get());
}

void CameraRig::update(LX_core::IInputState& input, float dt) {
  if (!m_camera) {
    throw std::runtime_error(
        "[lxe_editor] CameraRig::update called without attach()");
  }

  if (m_mode == Mode::Orbit) {
    m_orbit.update(m_camera->get(), input, dt);
  } else {
    m_freefly.update(m_camera->get(), input, dt);
  }
  m_camera->get().updateMatrices();
}

} // namespace LX_demo::lxe_editor
