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

[[nodiscard]] Mat4f invertMatrix(const Mat4f& matrix) {
  float m[16] = {};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      m[row * 4 + col] = matrix(row, col);
    }
  }

  float inv[16] = {};
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] -
           m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] +
           m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] -
           m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] +
           m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] -
           m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] +
           m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] -
           m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] +
           m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] +
           m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] -
           m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] -
            m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  float det =
      m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::abs(det) < 1e-8f) {
    return Mat4f::identity();
  }

  det = 1.0f / det;
  Mat4f result{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      result(row, col) = inv[row * 4 + col] * det;
    }
  }
  return result;
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

void CameraComponent::applyProjectionState(const CameraType projectionType,
                                           const float fovY,
                                           const float aspect,
                                           const float nearPlane,
                                           const float farPlane,
                                           const float leftValue,
                                           const float rightValue,
                                           const float bottomValue,
                                           const float topValue) {
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
  return getOwnerWorldTransform().rotation.rotate(Vec3f{0.0f, 0.0f, -1.0f})
      .normalized();
}

Vec3f CameraComponent::getUpVector() const {
  return getOwnerWorldTransform().rotation.rotate(Vec3f{0.0f, 1.0f, 0.0f})
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

Mat4f CameraComponent::getViewMatrix() const {
  const Transform worldTransform = getOwnerWorldTransform();
  const Quatf inverseRotation =
      worldTransform.rotation.conjugate().normalized();
  Transform viewTransform;
  viewTransform.translation =
      inverseRotation.rotate(-worldTransform.translation);
  viewTransform.rotation = inverseRotation;
  viewTransform.scale = Vec3f{1.0f, 1.0f, 1.0f};
  return viewTransform.toMat4();
}

Mat4f CameraComponent::getProjMatrix(float aspectOverride) const {
  const float projectionAspect =
      aspectOverride > 0.0f ? aspectOverride : m_aspect;
  if (m_type == CameraType::Perspective) {
    return Mat4f::perspective(m_fovY * kDegToRad, projectionAspect,
                              m_nearPlane, m_farPlane);
  }
  return Mat4f::orthographicDepthZeroToOne(
      m_left, m_right, m_bottom, m_top, -m_nearPlane, -m_farPlane);
}

Ray CameraComponent::pickRay(const Vec2f &screenPixel,
                             const Vec2f &viewportSize) const {
  const float viewportWidth = viewportSize.x > 0.0f ? viewportSize.x : 1.0f;
  const float viewportHeight = viewportSize.y > 0.0f ? viewportSize.y : 1.0f;
  const float ndcX = ((screenPixel.x + 0.5f) / viewportWidth) * 2.0f - 1.0f;
  const float ndcY = 1.0f - ((screenPixel.y + 0.5f) / viewportHeight) * 2.0f;

  if (m_type == CameraType::Perspective) {
    const float projectionAspect = viewportWidth / viewportHeight;
    const Mat4f viewProj =
        getProjMatrix(projectionAspect) * getViewMatrix();
    const Mat4f invViewProj = invertMatrix(viewProj);
    const Vec3f farPoint =
        (invViewProj * Vec4f{ndcX, ndcY, 1.0f, 1.0f}).toVec3();
    const Vec3f eye = getEyePosition();
    return Ray{eye, (farPoint - eye).normalized()};
  }

  const Transform worldTransform = getOwnerWorldTransform();
  const Vec3f eye = worldTransform.translation;
  const Vec3f rightAxis =
      worldTransform.rotation.rotate(Vec3f{1.0f, 0.0f, 0.0f}).normalized();
  const Vec3f upAxis =
      worldTransform.rotation.rotate(Vec3f{0.0f, 1.0f, 0.0f}).normalized();
  const Vec3f forwardAxis = getForwardVector();
  const float halfWidth = 0.5f * (m_right - m_left);
  const float halfHeight = 0.5f * (m_top - m_bottom);
  const float centerX = 0.5f * (m_left + m_right);
  const float centerY = 0.5f * (m_bottom + m_top);
  const Vec3f origin = eye + rightAxis * (centerX + ndcX * halfWidth) +
                       upAxis * (centerY + ndcY * halfHeight) +
                       forwardAxis * m_nearPlane;
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

  const float distance = (target - eye).length();
  if (distance > std::numeric_limits<float>::epsilon()) {
    m_lookDistance = distance;
  } else {
    m_lookDistance.reset();
  }
}

} // namespace LX_core
