#include "core/scene/freefly_camera_controller.hpp"
#include <algorithm>
#include <cmath>

namespace LX_core {

static constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

FreeFlyCameraController::FreeFlyCameraController(Vec3f startPos, float yawDeg,
                                                 float pitchDeg)
    : m_position(startPos), m_yawDeg(yawDeg), m_pitchDeg(pitchDeg) {}

void FreeFlyCameraController::update(CameraComponent &camera, const IInputState &input,
                                     float dt) {
  // Mouse look — only when right button is held
  if (input.isMouseButtonDown(MouseButton::Right)) {
    const auto delta = input.getMouseDelta();
    m_yawDeg -= delta.x * lookSpeedDegPerPixel;
    m_pitchDeg -= delta.y * lookSpeedDegPerPixel;
    m_pitchDeg = std::clamp(m_pitchDeg, minPitchDeg, maxPitchDeg);
  }

  // Compute basis vectors from yaw/pitch
  const float yawRad = m_yawDeg * kDegToRad;
  const float pitchRad = m_pitchDeg * kDegToRad;

  Vec3f forward;
  forward.x = std::cos(pitchRad) * std::sin(yawRad);
  forward.y = std::sin(pitchRad);
  forward.z = std::cos(pitchRad) * std::cos(yawRad);

  const Vec3f worldUp{0.0f, 1.0f, 0.0f};
  Vec3f right = forward.cross(worldUp).normalized();

  // Accumulate movement direction from keyboard
  Vec3f moveDir{0.0f, 0.0f, 0.0f};
  if (input.isKeyDown(KeyCode::W))
    moveDir += forward;
  if (input.isKeyDown(KeyCode::S))
    moveDir -= forward;
  if (input.isKeyDown(KeyCode::D))
    moveDir += right;
  if (input.isKeyDown(KeyCode::A))
    moveDir -= right;
  if (input.isKeyDown(KeyCode::Space))
    moveDir += worldUp;
  if (input.isKeyDown(KeyCode::LShift))
    moveDir -= worldUp;

  // Normalize to prevent diagonal speed boost
  float len = moveDir.length();
  if (len > 1e-6f) {
    moveDir = moveDir / len;

    float speed = moveSpeedPerSecond;
    if (input.isKeyDown(KeyCode::LCtrl)) {
      speed *= boostMultiplier;
    }

    m_position += moveDir * (speed * dt);
  }

  // Write back to camera
  camera.lookAt(m_position, m_position + forward, worldUp);
}

} // namespace LX_core
