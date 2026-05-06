#pragma once
#include "core/scene/camera_controller.hpp"

namespace LX_core {

class OrbitCameraController : public ICameraController {
public:
  OrbitCameraController(Vec3f target = {0, 0, 0}, float distance = 5.0f,
                        float yawDeg = 0.0f, float pitchDeg = 20.0f);

  void update(CameraComponent &camera, const IInputState &input, float dt) override;

  Vec3f getTarget() const { return m_target; }
  void setTarget(Vec3f t) { m_target = t; }

  float getDistance() const { return m_distance; }
  void setDistance(float d);

  float getYawDeg() const { return m_yawDeg; }
  float getPitchDeg() const { return m_pitchDeg; }
  void setYawDeg(float y) { m_yawDeg = y; }
  void setPitchDeg(float p) { m_pitchDeg = p; }

  float rotateSpeedDegPerPixel = 0.4f;
  float panSpeedPerPixel = 0.005f;
  float zoomSpeedPerWheel = 0.15f;
  float minDistance = 0.5f;
  float maxDistance = 200.0f;
  float minPitchDeg = -89.0f;
  float maxPitchDeg = 89.0f;

private:
  Vec3f m_target;
  float m_distance;
  float m_yawDeg;
  float m_pitchDeg;
};

} // namespace LX_core
