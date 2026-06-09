#include "core/scene/camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace LX_core {
namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

[[nodiscard]] Vec3f fallbackForward(Vec3f forward) {
  forward = forward.normalized();
  if (forward.length2() <= std::numeric_limits<float>::epsilon()) {
    return Vec3f{0.0f, 0.0f, -1.0f};
  }
  return forward;
}

[[nodiscard]] Vec3f fallbackUp(Vec3f up) {
  up = up.normalized();
  if (up.length2() <= std::numeric_limits<float>::epsilon()) {
    return Vec3f{0.0f, 1.0f, 0.0f};
  }
  return up;
}

} // namespace

CameraPose makeCameraPose(Vec3f eye, Vec3f forward, Vec3f up) {
  forward = fallbackForward(forward);
  up = fallbackUp(up);

  Vec3f right = forward.cross(up).normalized();
  if (right.length2() <= std::numeric_limits<float>::epsilon()) {
    const Vec3f fallback =
        std::abs(forward.y) > 0.99f ? Vec3f{1.0f, 0.0f, 0.0f}
                                    : Vec3f{0.0f, 1.0f, 0.0f};
    right = forward.cross(fallback).normalized();
  }
  up = right.cross(forward).normalized();
  return CameraPose{.eye = eye, .forward = forward, .up = up};
}

Mat4f makeCameraViewMatrix(const CameraPose &pose) {
  return Mat4f::lookAt(pose.eye, pose.eye + pose.forward, pose.up);
}

Mat4f makeCameraProjectionMatrix(const CameraProjection &projection,
                                 GraphicsAPI api) {
  if (projection.type == CameraType::Perspective) {
    const float aspect = projection.aspect > 0.0f ? projection.aspect : 1.0f;
    return Mat4f::perspective(projection.fovYDegrees * kDegToRad, aspect,
                              projection.nearPlane, projection.farPlane, api);
  }
  return Mat4f::orthographic(projection.left, projection.right,
                             projection.bottom, projection.top,
                             projection.nearPlane, projection.farPlane, api);
}

CameraRayFrame makeCameraRayFrame(const CameraPose &pose,
                                  const CameraProjection &projection) {
  const CameraPose resolvedPose =
      makeCameraPose(pose.eye, pose.forward, pose.up);
  const Vec3f unitRight =
      resolvedPose.forward.cross(resolvedPose.up).normalized();
  if (projection.type == CameraType::Perspective) {
    const float aspect = projection.aspect > 0.0f ? projection.aspect : 1.0f;
    const float tanHalfFov =
        std::tan(projection.fovYDegrees * kDegToRad * 0.5f);
    return CameraRayFrame{
        .eye = resolvedPose.eye,
        .right = unitRight * (tanHalfFov * aspect),
        .up = resolvedPose.up * tanHalfFov,
        .forward = resolvedPose.forward,
    };
  }

  const float halfWidth = 0.5f * (projection.right - projection.left);
  const float halfHeight = 0.5f * (projection.top - projection.bottom);
  return CameraRayFrame{
      .eye = resolvedPose.eye,
      .right = unitRight * halfWidth,
      .up = resolvedPose.up * halfHeight,
      .forward = resolvedPose.forward,
  };
}

Ray makeCameraRay(const CameraPose &pose, const CameraProjection &projection,
                  const Vec2f &screenPixel, const Vec2f &viewportSize) {
  const float viewportWidth = viewportSize.x > 0.0f ? viewportSize.x : 1.0f;
  const float viewportHeight = viewportSize.y > 0.0f ? viewportSize.y : 1.0f;
  const float ndcX = ((screenPixel.x + 0.5f) / viewportWidth) * 2.0f - 1.0f;
  const float ndcY = 1.0f - ((screenPixel.y + 0.5f) / viewportHeight) * 2.0f;

  const CameraRayFrame frame = makeCameraRayFrame(pose, projection);
  if (projection.type == CameraType::Perspective) {
    const Vec3f direction =
        (frame.forward + frame.right * ndcX + frame.up * ndcY).normalized();
    return Ray{frame.eye + direction * projection.nearPlane, direction};
  }

  const Vec3f origin = frame.eye + frame.right * ndcX + frame.up * ndcY +
                       frame.forward * projection.nearPlane;
  return Ray{origin, frame.forward};
}

} // namespace LX_core
