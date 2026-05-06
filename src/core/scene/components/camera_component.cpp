#include "camera_component.hpp"

#include "core/scene/object.hpp"

#include <cmath>
#include <limits>

namespace LX_core {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

float safeScaleComponent(float value) {
  return std::abs(value) <= std::numeric_limits<float>::epsilon() ? 1.0f : value;
}

Transform toLocalFromWorld(const SceneNode &node, const Transform &worldTransform) {
  auto parent = node.getParent();
  if (!parent) {
    return worldTransform.normalized();
  }

  const Transform parentWorld = Transform::fromMat4(parent->getWorldTransform());
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
    const Vec3f fallbackUp =
        std::abs(forward.y) > 0.99f ? Vec3f{1.0f, 0.0f, 0.0f}
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

Vec3f CameraComponent::getEyePosition() const {
  return getOwnerWorldTransform().translation;
}

Vec3f CameraComponent::getForwardVector() const {
  return getOwnerWorldTransform().rotation.rotate(Vec3f{0.0f, 0.0f, -1.0f})
      .normalized();
}

Vec3f CameraComponent::getUpVector() const {
  return getOwnerWorldTransform().rotation.rotate(Vec3f{0.0f, 1.0f, 0.0f})
      .normalized();
}

Vec3f CameraComponent::getLookTarget(float distance) const {
  return getEyePosition() + getForwardVector() * distance;
}

Mat4f CameraComponent::getViewMatrix() const {
  const Vec3f eye = getEyePosition();
  const Vec3f forward = getForwardVector();
  const Vec3f up = getUpVector();
  return Mat4f::lookAt(eye, eye + forward, up);
}

Mat4f CameraComponent::getProjMatrix(float aspectOverride) const {
  const float projectionAspect = aspectOverride > 0.0f ? aspectOverride : aspect;
  if (type == CameraType::Perspective) {
    return Mat4f::perspective(fovY * kDegToRad, projectionAspect, nearPlane,
                              farPlane);
  }
  return Mat4f::orthographic(left, right, bottom, top, nearPlane, farPlane);
}

Ray CameraComponent::pickRay(const Vec2f &screenPixel,
                             const Vec2f &viewportSize) const {
  const float viewportWidth = viewportSize.x > 0.0f ? viewportSize.x : 1.0f;
  const float viewportHeight = viewportSize.y > 0.0f ? viewportSize.y : 1.0f;
  const float ndcX = ((screenPixel.x + 0.5f) / viewportWidth) * 2.0f - 1.0f;
  const float ndcY = 1.0f - ((screenPixel.y + 0.5f) / viewportHeight) * 2.0f;

  const Transform worldTransform = getOwnerWorldTransform();
  const Vec3f eye = worldTransform.translation;
  const Vec3f rightAxis =
      worldTransform.rotation.rotate(Vec3f{1.0f, 0.0f, 0.0f}).normalized();
  const Vec3f upAxis =
      worldTransform.rotation.rotate(Vec3f{0.0f, 1.0f, 0.0f}).normalized();
  const Vec3f forwardAxis = getForwardVector();

  if (type == CameraType::Perspective) {
    const float projectionAspect = aspect > 0.0f ? aspect : viewportWidth / viewportHeight;
    const float tanHalfFov = std::tan(0.5f * fovY * kDegToRad);
    const float halfHeight = tanHalfFov * nearPlane;
    const float halfWidth = halfHeight * projectionAspect;
    const Vec3f nearPoint = eye + forwardAxis * nearPlane +
                            rightAxis * (ndcX * halfWidth) +
                            upAxis * (ndcY * halfHeight);
    return Ray{eye, (nearPoint - eye).normalized()};
  }

  const float halfWidth = 0.5f * (right - left);
  const float halfHeight = 0.5f * (top - bottom);
  const float centerX = 0.5f * (left + right);
  const float centerY = 0.5f * (bottom + top);
  const Vec3f origin = eye + rightAxis * (centerX + ndcX * halfWidth) +
                       upAxis * (centerY + ndcY * halfHeight) +
                       forwardAxis * nearPlane;
  return Ray{origin, forwardAxis};
}

void CameraComponent::updateMatrices() {
  m_ubo->param.eyePos = getEyePosition();
  m_ubo->param.view = getViewMatrix();
  m_ubo->param.proj = getProjMatrix();
  m_ubo->setDirty();
}

void CameraComponent::setPosition(const Vec3f &position) {
  auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }

  auto &node = ownerNode->get();
  auto local = node.getLocalTransform();
  if (auto parent = node.getParent()) {
    const Transform parentWorld = Transform::fromMat4(parent->getWorldTransform());
    const Quatf invParentRotation = parentWorld.rotation.conjugate().normalized();
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
}

} // namespace LX_core
