#include "demos/scene_viewer/editor_camera_state.hpp"

#include "core/math/quat.hpp"
#include "core/math/transform.hpp"

#include <cmath>

namespace LX_demo::scene_viewer {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

[[nodiscard]] LX_core::Quatf eulerDegreesToQuat(
    const LX_core::Vec3f& degrees) {
  const LX_core::Quatf qx = LX_core::Quatf::fromAxisAngle(
      LX_core::Vec3f{1.0f, 0.0f, 0.0f}, degrees.x * kDegToRad);
  const LX_core::Quatf qy = LX_core::Quatf::fromAxisAngle(
      LX_core::Vec3f{0.0f, 1.0f, 0.0f}, degrees.y * kDegToRad);
  const LX_core::Quatf qz = LX_core::Quatf::fromAxisAngle(
      LX_core::Vec3f{0.0f, 0.0f, 1.0f}, degrees.z * kDegToRad);
  return (qz * qy * qx).normalized();
}

[[nodiscard]] LX_core::Vec3f quatToEulerDegrees(const LX_core::Quatf& quat) {
  const LX_core::Quatf q = quat.normalized();

  const float sinrCosp = 2.0f * (q.w * q.v.x + q.v.y * q.v.z);
  const float cosrCosp = 1.0f - 2.0f * (q.v.x * q.v.x + q.v.y * q.v.y);
  const float roll = std::atan2(sinrCosp, cosrCosp);

  const float sinp = 2.0f * (q.w * q.v.y - q.v.z * q.v.x);
  const float pitch = std::abs(sinp) >= 1.0f
                          ? std::copysign(0.5f * kPi, sinp)
                          : std::asin(sinp);

  const float sinyCosp = 2.0f * (q.w * q.v.z + q.v.x * q.v.y);
  const float cosyCosp = 1.0f - 2.0f * (q.v.y * q.v.y + q.v.z * q.v.z);
  const float yaw = std::atan2(sinyCosp, cosyCosp);

  return LX_core::Vec3f{roll * kRadToDeg, pitch * kRadToDeg,
                        yaw * kRadToDeg};
}

} // namespace

EditorCameraState EditorCameraState::captureFrom(
    const LX_core::SceneNode& node, const LX_core::CameraComponent& camera) {
  return EditorCameraState{
      .position = node.getTranslation(),
      .rotationEulerDeg = quatToEulerDegrees(node.getRotation()),
      .fovY = camera.fovY,
      .nearPlane = camera.nearPlane,
      .farPlane = camera.farPlane,
  };
}

void EditorCameraState::applyTo(LX_core::SceneNode& node,
                                LX_core::CameraComponent& camera) const {
  applyToNode(node);
  applyToCamera(camera);
}

void EditorCameraState::applyToNode(LX_core::SceneNode& node) const {
  LX_core::Transform transform = node.getLocalTransform();
  transform.translation = position;
  transform.rotation = eulerDegreesToQuat(rotationEulerDeg);
  node.setLocalTransform(transform.normalized());
}

void EditorCameraState::applyToCamera(LX_core::CameraComponent& camera) const {
  camera.fovY = fovY;
  camera.nearPlane = nearPlane;
  camera.farPlane = farPlane;
  camera.updateMatrices();
}

} // namespace LX_demo::scene_viewer
