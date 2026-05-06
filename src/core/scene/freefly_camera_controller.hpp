#pragma once
#include "core/scene/camera_controller.hpp"

namespace LX_core {

class FreeFlyCameraController : public ICameraController {
public:
  explicit FreeFlyCameraController(Vec3f startPos = {0, 0, 5},
                                   float yawDeg = 180.0f,
                                   float pitchDeg = 0.0f);

  void update(CameraComponent &camera, const IInputState &input, float dt) override;

  Vec3f getPosition() const { return m_position; }
  float getYawDeg() const { return m_yawDeg; }
  float getPitchDeg() const { return m_pitchDeg; }

  void setPosition(Vec3f p) { m_position = p; }
  void setYawDeg(float y) { m_yawDeg = y; }
  void setPitchDeg(float p) { m_pitchDeg = p; }

  float moveSpeedPerSecond = 4.0f;
  float boostMultiplier = 4.0f;
  float lookSpeedDegPerPixel = 0.15f;
  float minPitchDeg = -89.0f;
  float maxPitchDeg = 89.0f;

private:
  Vec3f m_position;
  float m_yawDeg;
  float m_pitchDeg;
};

} // namespace LX_core
