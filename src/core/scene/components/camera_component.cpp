#include "camera_component.hpp"

#include "core/scene/object.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace LX_core {

namespace {

float safeScaleComponent(float value) {
  return std::abs(value) <= std::numeric_limits<float>::epsilon() ? 1.0f
                                                                  : value;
}

Transform toLocalFromWorld(const SceneNode &node,
                           const Transform &worldTransform) {
  auto parent = node.getParent();
  if (!parent) {
    return worldTransform.normalized();
  }

  const Transform parentWorld =
      Transform::fromMat4(parent->getWorldTransform());
  const Quatf invParentRotation = parentWorld.rotation.conjugate().normalized();

  Vec3f delta = worldTransform.translation - parentWorld.translation;
  delta.x /= safeScaleComponent(parentWorld.scale.x);
  delta.y /= safeScaleComponent(parentWorld.scale.y);
  delta.z /= safeScaleComponent(parentWorld.scale.z);

  Transform local = worldTransform.normalized();
  local.translation = invParentRotation.rotate(delta);
  local.rotation = (invParentRotation * worldTransform.rotation).normalized();
  return local;
}

Transform makeWorldLookAt(const Vec3f &eye, const Vec3f &target,
                          const Vec3f &upHint) {
  Vec3f forward = (target - eye).normalized();
  if (forward.length2() <= std::numeric_limits<float>::epsilon()) {
    forward = Vec3f{0.0f, 0.0f, -1.0f};
  }

  Vec3f up = upHint.normalized();
  if (up.length2() <= std::numeric_limits<float>::epsilon()) {
    up = Vec3f{0.0f, 1.0f, 0.0f};
  }

  Vec3f back = (-forward).normalized();
  Vec3f right = up.cross(back);
  if (right.length2() <= std::numeric_limits<float>::epsilon()) {
    const Vec3f fallbackUp = std::abs(forward.y) > 0.99f
                                 ? Vec3f{1.0f, 0.0f, 0.0f}
                                 : Vec3f{0.0f, 1.0f, 0.0f};
    right = fallbackUp.cross(back);
  }
  right = right.normalized();
  Vec3f correctedUp = back.cross(right).normalized();

  Mat4f world = Mat4f::identity();
  world(0, 0) = right.x;
  world(1, 0) = right.y;
  world(2, 0) = right.z;
  world(0, 1) = correctedUp.x;
  world(1, 1) = correctedUp.y;
  world(2, 1) = correctedUp.z;
  world(0, 2) = back.x;
  world(1, 2) = back.y;
  world(2, 2) = back.z;
  world(0, 3) = eye.x;
  world(1, 3) = eye.y;
  world(2, 3) = eye.z;
  return Transform::fromMat4(world);
}

} // namespace

Transform CameraComponent::getOwnerWorldTransform() const {
  auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return Transform::identity();
  }
  return Transform::fromMat4(ownerNode->get().getWorldTransform());
}

void CameraComponent::setProjectionType(const CameraType projectionType) {
  m_type = projectionType;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setFovY(const float value) {
  m_fovY = value;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setAspect(const float value) {
  m_aspect = value;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setNearPlane(const float value) {
  m_nearPlane = value;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setFarPlane(const float value) {
  m_farPlane = value;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setOrthographicBounds(const float leftValue,
                                            const float rightValue,
                                            const float bottomValue,
                                            const float topValue) {
  m_left = leftValue;
  m_right = rightValue;
  m_bottom = bottomValue;
  m_top = topValue;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::applyProjectionState(
    const CameraType projectionType, const float fovY, const float aspect,
    const float nearPlane, const float farPlane, const float leftValue,
    const float rightValue, const float bottomValue, const float topValue) {
  m_type = projectionType;
  m_fovY = fovY;
  m_aspect = aspect;
  m_nearPlane = nearPlane;
  m_farPlane = farPlane;
  m_left = leftValue;
  m_right = rightValue;
  m_bottom = bottomValue;
  m_top = topValue;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setTarget(RenderTarget target) {
  m_target = std::move(target);
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setTarget(std::optional<RenderTarget> target) {
  m_target = std::move(target);
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::clearTarget() {
  m_target.reset();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setCullingMask(const VisibilityLayerMask mask) {
  m_cullingMask = mask;
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setActive(const bool active) {
  m_active = active;
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

Vec3f CameraComponent::getEyePosition() const {
  return getOwnerWorldTransform().translation;
}

Vec3f CameraComponent::getForwardVector() const {
  return getOwnerWorldTransform()
      .rotation.rotate(Vec3f{0.0f, 0.0f, -1.0f})
      .normalized();
}

Vec3f CameraComponent::getUpVector() const {
  return getOwnerWorldTransform()
      .rotation.rotate(Vec3f{0.0f, 1.0f, 0.0f})
      .normalized();
}

Vec3f CameraComponent::getLookTarget(float distance) const {
  const float resolvedDistance =
      m_lookDistance.has_value() ? *m_lookDistance : distance;
  return getEyePosition() + getForwardVector() * resolvedDistance;
}

BoundingBox CameraComponent::getDebugLocalBounds() const {
  constexpr float radius = 0.18f;
  return BoundingBox{Vec3f{-radius, -radius, -radius},
                     Vec3f{radius, radius, radius}};
}

CameraPose CameraComponent::getPose() const {
  return makeCameraPose(getEyePosition(), getForwardVector(), getUpVector());
}

CameraProjection CameraComponent::getProjection() const {
  return CameraProjection{
      .type = m_type,
      .fovYDegrees = m_fovY,
      .aspect = m_aspect,
      .nearPlane = m_nearPlane,
      .farPlane = m_farPlane,
      .left = m_left,
      .right = m_right,
      .bottom = m_bottom,
      .top = m_top,
  };
}

CameraSnapshot CameraComponent::getSnapshot(std::string path) const {
  return CameraSnapshot{
      .path = std::move(path),
      .pose = getPose(),
      .projection = getProjection(),
      .cullingMask = m_cullingMask,
      .active = m_active,
  };
}

Mat4f CameraComponent::getProjMatrix(float aspectOverride,
                                     GraphicsAPI api) const {
  CameraProjection projection = getProjection();
  if (aspectOverride > 0.0f) {
    projection.aspect = aspectOverride;
  }
  return makeCameraProjectionMatrix(projection, api);
}

Mat4f CameraComponent::getViewMatrix() const {
  return makeCameraViewMatrix(getPose());
}

Ray CameraComponent::pickRay(const Vec2f &screenPixel,
                             const Vec2f &viewportSize) const {
  const float viewportWidth = viewportSize.x > 0.0f ? viewportSize.x : 1.0f;
  const float viewportHeight = viewportSize.y > 0.0f ? viewportSize.y : 1.0f;
  CameraProjection projection = getProjection();
  projection.aspect = viewportWidth / viewportHeight;
  return makeCameraRay(getPose(), projection, screenPixel, viewportSize);
}

void CameraComponent::updateMatrices() {
}

void CameraComponent::setPosition(const Vec3f &position) {
  auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }

  auto &node = ownerNode->get();
  auto local = node.getLocalTransform();
  if (auto parent = node.getParent()) {
    const Transform parentWorld =
        Transform::fromMat4(parent->getWorldTransform());
    const Quatf invParentRotation =
        parentWorld.rotation.conjugate().normalized();
    Vec3f delta = position - parentWorld.translation;
    delta.x /= safeScaleComponent(parentWorld.scale.x);
    delta.y /= safeScaleComponent(parentWorld.scale.y);
    delta.z /= safeScaleComponent(parentWorld.scale.z);
    local.translation = invParentRotation.rotate(delta);
  } else {
    local.translation = position;
  }
  node.setLocalTransform(local);
}

void CameraComponent::lookAt(const Vec3f &eye, const Vec3f &target,
                             const Vec3f &up) {
  auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }

  auto &node = ownerNode->get();
  Transform local = toLocalFromWorld(node, makeWorldLookAt(eye, target, up));
  local.scale = node.getLocalTransform().scale;
  node.setLocalTransform(local);

  const float distance = (target - eye).length();
  if (distance > std::numeric_limits<float>::epsilon()) {
    m_lookDistance = distance;
  } else {
    m_lookDistance.reset();
  }
}

} // namespace LX_core
