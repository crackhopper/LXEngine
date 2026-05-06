#include "core/scene/orbit_camera_controller.hpp"
#include <algorithm>
#include <cmath>

namespace LX_core {

static constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

OrbitCameraController::OrbitCameraController(Vec3f target, float distance,
                                             float yawDeg, float pitchDeg)
    : m_target(target), m_distance(distance), m_yawDeg(yawDeg),
      m_pitchDeg(pitchDeg) {}

void OrbitCameraController::setDistance(float d) {
  m_distance = std::clamp(d, minDistance, maxDistance);
}

void OrbitCameraController::update(CameraComponent &camera, const IInputState &input,
                                   float /*dt*/) {
  const auto mouseDelta = input.getMouseDelta();
  const float wheel = input.getMouseWheelDelta();

  // Left-drag: rotate
  if (input.isMouseButtonDown(MouseButton::Left)) {
    m_yawDeg += mouseDelta.x * rotateSpeedDegPerPixel;
    m_pitchDeg -= mouseDelta.y * rotateSpeedDegPerPixel;
    m_pitchDeg = std::clamp(m_pitchDeg, minPitchDeg, maxPitchDeg);
  }

  // Right-drag: pan target
  if (input.isMouseButtonDown(MouseButton::Right)) {
    const float yawRad = m_yawDeg * kDegToRad;
    const float pitchRad = m_pitchDeg * kDegToRad;

    // Camera-local right vector (lies in XZ plane when pitch=0)
    Vec3f right{std::cos(yawRad), 0.0f, -std::sin(yawRad)};
    // Camera-local up vector (perpendicular to forward and right)
    Vec3f forward{-std::cos(pitchRad) * std::sin(yawRad),
                  -std::sin(pitchRad),
                  -std::cos(pitchRad) * std::cos(yawRad)};
    Vec3f up = right.cross(forward).normalized();

    m_target -= right * (mouseDelta.x * panSpeedPerPixel);
    m_target += up * (mouseDelta.y * panSpeedPerPixel);
  }

  // Scroll: zoom
  if (std::abs(wheel) > 1e-6f) {
    m_distance *= (1.0f - wheel * zoomSpeedPerWheel);
    m_distance = std::clamp(m_distance, minDistance, maxDistance);
  }

  // Compute camera position from orbit parameters
  const float yawRad = m_yawDeg * kDegToRad;
  const float pitchRad = m_pitchDeg * kDegToRad;

  Vec3f eye;
  eye.x = m_target.x + m_distance * std::cos(pitchRad) * std::sin(yawRad);
  eye.y = m_target.y + m_distance * std::sin(pitchRad);
  eye.z = m_target.z + m_distance * std::cos(pitchRad) * std::cos(yawRad);

  camera.lookAt(eye, m_target, Vec3f(0.0f, 1.0f, 0.0f));
}

} // namespace LX_core
