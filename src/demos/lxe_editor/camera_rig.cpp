#include "camera_rig.hpp"

#include "core/debug_draw/debug_draw.hpp"
#include "core/input/key_code.hpp"
#include "core/input/mouse_button.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace LX_demo::lxe_editor {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kOrbitTargetMarkerRadius = 0.08f;
constexpr float kOrbitTargetKeyboardSpeed = 1.5f;

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

void setOrbitTargetKeepingEye(LX_core::OrbitCameraController& ctrl,
                              LX_core::CameraComponent& camera,
                              const LX_core::Vec3f& target) {
  const LX_core::Vec3f eye = camera.getEyePosition();
  const LX_core::Vec3f offset = eye - target;
  const float distance = offset.length();
  ctrl.setTarget(target);
  if (distance > 1e-6f) {
    ctrl.setDistance(distance);
    ctrl.setYawDeg(std::atan2(offset.x, offset.z) * 180.0f / kPi);
    ctrl.setPitchDeg(std::asin(clampUnit(offset.y / distance)) * 180.0f / kPi);
  }
  camera.lookAt(eye, target, LX_core::Vec3f{0.0f, 1.0f, 0.0f});
  camera.updateMatrices();
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

void CameraRig::handleOrbitTargetControls(const LX_core::IInputState& input,
                                          const LX_core::Scene& scene,
                                          const SceneViewRect& sceneViewRect,
                                          const float dt) {
  if (m_mode != Mode::Orbit || !m_camera) {
    m_prevOrbitPickKeyDown = input.isKeyDown(LX_core::KeyCode::M);
    return;
  }

  LX_core::CameraComponent& camera = m_camera->get();
  const bool rightHeld = input.isMouseButtonDown(LX_core::MouseButton::Right);
  const bool pickKeyDown = input.isKeyDown(LX_core::KeyCode::M);
  if (rightHeld && pickKeyDown && !m_prevOrbitPickKeyDown &&
      sceneViewRect.isValid() && sceneViewRect.contains(input.getMousePosition())) {
    const LX_core::Vec2f localPixel =
        sceneViewRect.localPixel(input.getMousePosition());
    const LX_core::Ray ray = camera.pickRay(localPixel, sceneViewRect.size());
    const auto hit = scene.pick(ray, LX_core::Layer_All & ~LX_core::Layer_EditorOverlay);
    if (hit.has_value()) {
      setOrbitTargetKeepingEye(
          m_orbit, camera, ray.origin + ray.direction * hit->distance);
    }
  }
  m_prevOrbitPickKeyDown = pickKeyDown;

  if (!rightHeld || dt <= 0.0f) {
    return;
  }

  LX_core::Vec3f direction{0.0f, 0.0f, 0.0f};
  const LX_core::Vec3f forward = camera.getForwardVector().normalized();
  const LX_core::Vec3f up = camera.getUpVector().normalized();
  const LX_core::Vec3f right = forward.cross(up).normalized();
  if (input.isKeyDown(LX_core::KeyCode::W)) {
    direction += up;
  }
  if (input.isKeyDown(LX_core::KeyCode::S)) {
    direction -= up;
  }
  if (input.isKeyDown(LX_core::KeyCode::D)) {
    direction += right;
  }
  if (input.isKeyDown(LX_core::KeyCode::A)) {
    direction -= right;
  }
  if (direction.length2() <= 1e-6f) {
    return;
  }

  const float speed =
      std::max(0.1f, m_orbit.getDistance()) * kOrbitTargetKeyboardSpeed;
  m_orbit.setTarget(m_orbit.getTarget() + direction.normalized() * speed * dt);
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

void CameraRig::enqueueDebugDraw() const {
  if (m_mode != Mode::Orbit) {
    return;
  }

  const LX_core::Vec3f target = m_orbit.getTarget();
  const LX_core::Vec4f color{0.2f, 1.0f, 1.0f, 1.0f};
  LX_core::DebugDraw::wireSphere(target, kOrbitTargetMarkerRadius, color, 12);
  LX_core::DebugDraw::drawLine(
      target - LX_core::Vec3f{kOrbitTargetMarkerRadius, 0.0f, 0.0f},
      target + LX_core::Vec3f{kOrbitTargetMarkerRadius, 0.0f, 0.0f}, color);
  LX_core::DebugDraw::drawLine(
      target - LX_core::Vec3f{0.0f, kOrbitTargetMarkerRadius, 0.0f},
      target + LX_core::Vec3f{0.0f, kOrbitTargetMarkerRadius, 0.0f}, color);
  LX_core::DebugDraw::drawLine(
      target - LX_core::Vec3f{0.0f, 0.0f, kOrbitTargetMarkerRadius},
      target + LX_core::Vec3f{0.0f, 0.0f, kOrbitTargetMarkerRadius}, color);
}

} // namespace LX_demo::lxe_editor
