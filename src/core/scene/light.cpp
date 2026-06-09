#include "light.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace LX_core {
namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

void setComponent(Vec4f &v, u32 index, float value) {
  switch (index) {
  case 0:
    v.x = value;
    break;
  case 1:
    v.y = value;
    break;
  case 2:
    v.z = value;
    break;
  default:
    v.w = value;
    break;
  }
}

[[nodiscard]] float getComponent(const Vec4f &v, u32 index) {
  switch (index) {
  case 0:
    return v.x;
  case 1:
    return v.y;
  case 2:
    return v.z;
  default:
    return v.w;
  }
}

[[nodiscard]] Vec3f lightDirectionFromNode(const SceneNode &node) {
  return Transform::fromMat4(node.getWorldTransform())
      .rotation.rotate(Vec3f{0.0f, 0.0f, -1.0f})
      .normalized();
}

[[nodiscard]] Quatf worldRotationForLightDirection(Vec3f direction) {
  if (direction.length2() <= 1e-6f) {
    direction = Vec3f{0.0f, 0.0f, -1.0f};
  }
  const Vec3f forward = direction.normalized();
  Vec3f up{0.0f, 1.0f, 0.0f};
  if (std::abs(forward.dot(up)) > 0.95f) {
    up = Vec3f{1.0f, 0.0f, 0.0f};
  }
  const Vec3f back = (-forward).normalized();
  const Vec3f right = up.cross(back).normalized();
  const Vec3f correctedUp = back.cross(right).normalized();

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
  return Transform::fromMat4(world).rotation.normalized();
}

void rotateLightNodeToDirection(SceneNode &node, const Vec3f &direction) {
  const Quatf worldRotation = worldRotationForLightDirection(direction);
  if (const auto parent = node.getParent()) {
    const Transform parentWorld =
        Transform::fromMat4(parent->getWorldTransform());
    node.setRotation(
        (parentWorld.rotation.conjugate().normalized() * worldRotation)
            .normalized());
    return;
  }
  node.setRotation(worldRotation);
}

void emitLightPropertyChanged(const std::weak_ptr<Scene> &weakScene,
                              const std::weak_ptr<SceneNode> &weakNode) {
  const auto scene = weakScene.lock();
  const auto node = weakNode.lock();
  if (!scene || !node) {
    return;
  }

  scene->events().emit(SceneEvent{
      .domain = SceneEventDomain::Runtime,
      .type = SceneEventType::SceneNodeChanged,
      .path = node->getPath(),
      .stableNodeName = node->getNodeName(),
      .aspects = {SceneNodeAspect::LightProperties},
  });
}

} // namespace

DirectionalLight::DirectionalLight()
    : m_ubo(std::make_unique<DirectionalLightData>()),
      m_supportedPasses({Pass_Forward, Pass_Deferred, Pass_Shadow}) {
  const Vec3f defaultDirection = m_pendingDirection.normalized();
  m_ubo->param.dir =
      Vec4f{defaultDirection.x, defaultDirection.y, defaultDirection.z, 0.0f};
  m_ubo->param.color = Vec4f{1.0f, 0.98f, 0.9f, 1.0f};
  m_ubo->param.shadowParams =
      Vec4f{1024.0f, 0.02f, 0.45f, static_cast<float>(MaxShadowCascades)};
  updateShadowViewProjection();
}

std::unique_ptr<LightBase> DirectionalLight::cloneUnique() const {
  auto clone = std::make_unique<DirectionalLight>();
  clone->m_ubo = std::make_unique<DirectionalLightData>(*m_ubo);
  clone->m_pendingDirection = m_pendingDirection;
  std::copy(std::begin(m_shadowCascadeDebugViews),
            std::end(m_shadowCascadeDebugViews),
            std::begin(clone->m_shadowCascadeDebugViews));
  std::copy(std::begin(m_shadowCascadeDebugViewValid),
            std::end(m_shadowCascadeDebugViewValid),
            std::begin(clone->m_shadowCascadeDebugViewValid));
  clone->m_shadowDistance = m_shadowDistance;
  clone->m_supportedPasses = m_supportedPasses;
  return clone;
}

Vec3f DirectionalLight::getDirection() const {
  if (const auto node = m_node.lock()) {
    return lightDirectionFromNode(*node);
  }
  if (m_pendingDirection.length2() <= 1e-6f) {
    return Vec3f{0.0f, 0.0f, -1.0f};
  }
  return m_pendingDirection.normalized();
}

Vec3f DirectionalLight::getColor() const {
  return Vec3f{m_ubo->param.color.x, m_ubo->param.color.y,
               m_ubo->param.color.z};
}

float DirectionalLight::getIntensity() const { return m_ubo->param.color.w; }

Mat4f DirectionalLight::getShadowViewProj() const {
  return m_ubo->param.shadowViewProj;
}

Vec4f DirectionalLight::getShadowParams() const {
  return m_ubo->param.shadowParams;
}

float DirectionalLight::getShadowDistance() const { return m_shadowDistance; }

Vec4f DirectionalLight::getCascadeSplits() const {
  return m_ubo->param.cascadeSplits;
}

u32 DirectionalLight::getShadowCascadeCount() const {
  return static_cast<u32>(std::clamp(m_ubo->param.shadowParams.w, 1.0f,
                                     static_cast<float>(MaxShadowCascades)));
}

std::shared_ptr<SceneNode> DirectionalLight::getSceneNode() const {
  return m_node.lock();
}

void DirectionalLight::setDirection(const Vec3f &direction) {
  if (const auto node = m_node.lock()) {
    rotateLightNodeToDirection(*node, direction);
  } else {
    m_pendingDirection = direction;
  }
  const Vec3f resolved = getDirection();
  m_ubo->param.dir = Vec4f{resolved.x, resolved.y, resolved.z, 0.0f};
  updateShadowViewProjection();
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setColor(const Vec3f &color) {
  m_ubo->param.color.x = color.x;
  m_ubo->param.color.y = color.y;
  m_ubo->param.color.z = color.z;
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setIntensity(const float intensity) {
  m_ubo->param.color.w = intensity;
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setShadowMapSize(float size) {
  if (size < 1.0f) {
    size = 1.0f;
  }
  m_ubo->param.shadowParams.x = size;
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setShadowBias(float bias) {
  m_ubo->param.shadowParams.y = std::max(0.0f, bias);
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setShadowStrength(float strength) {
  m_ubo->param.shadowParams.z = std::clamp(strength, 0.0f, 1.0f);
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setShadowCascadeCount(u32 count) {
  count = std::clamp(count, 1u, MaxShadowCascades);
  m_ubo->param.shadowParams.w = static_cast<float>(count);
  setActiveShadowCascade(0);
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setShadowDistance(float distance) {
  m_shadowDistance = std::max(1.0f, distance);
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::updateShadowCascadesForCamera(
    const CameraComponent &camera, float splitLambda) {
  splitLambda = std::clamp(splitLambda, 0.0f, 1.0f);
  const u32 cascadeCount = getShadowCascadeCount();
  const float nearPlane = std::max(0.001f, camera.getNearPlane());
  const float farPlane = std::max(
      nearPlane + 0.001f, std::min(camera.getFarPlane(), m_shadowDistance));
  const float clipRange = farPlane - nearPlane;

  Vec3f lightDir = getDirection();
  if (std::abs(lightDir.x) < 1e-5f && std::abs(lightDir.y) < 1e-5f &&
      std::abs(lightDir.z) < 1e-5f) {
    lightDir = Vec3f{0.35f, -1.0f, 0.25f};
  }
  lightDir = lightDir.normalized();
  m_ubo->param.dir = Vec4f{lightDir.x, lightDir.y, lightDir.z, 0.0f};

  Vec3f lightUp{0.0f, 1.0f, 0.0f};
  if (std::abs(lightDir.dot(lightUp)) > 0.95f) {
    lightUp = Vec3f{0.0f, 0.0f, 1.0f};
  }

  const Vec3f eye = camera.getEyePosition();
  const Vec3f forward = camera.getForwardVector().normalized();
  const Vec3f up = camera.getUpVector().normalized();
  const Vec3f right = forward.cross(up).normalized();
  const float tanHalfFov = std::tan(camera.getFovY() * kDegToRad * 0.5f);
  const float aspect = std::max(0.001f, camera.getAspect());

  float previousSplit = nearPlane;
  for (u32 cascadeIndex = 0; cascadeIndex < MaxShadowCascades; ++cascadeIndex) {
    if (cascadeIndex >= cascadeCount) {
      setComponent(m_ubo->param.cascadeSplits, cascadeIndex, farPlane);
      setComponent(
          m_ubo->param.cascadeDepthRanges, cascadeIndex,
          getComponent(m_ubo->param.cascadeDepthRanges, cascadeCount - 1u));
      m_ubo->param.cascadeViewProj[cascadeIndex] =
          m_ubo->param.cascadeViewProj[cascadeCount - 1u];
      m_shadowCascadeDebugViews[cascadeIndex] =
          m_shadowCascadeDebugViews[cascadeCount - 1u];
      m_shadowCascadeDebugViewValid[cascadeIndex] =
          m_shadowCascadeDebugViewValid[cascadeCount - 1u];
      continue;
    }

    const float p = static_cast<float>(cascadeIndex + 1u) /
                    static_cast<float>(cascadeCount);
    const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
    const float uniformSplit = nearPlane + clipRange * p;
    const float split =
        splitLambda * logSplit + (1.0f - splitLambda) * uniformSplit;
    setComponent(m_ubo->param.cascadeSplits, cascadeIndex, split);

    std::vector<Vec3f> corners;
    corners.reserve(8);
    const auto appendCorners = [&](float distance) {
      const float halfHeight = tanHalfFov * distance;
      const float halfWidth = halfHeight * aspect;
      const Vec3f center = eye + forward * distance;
      corners.push_back(center + right * halfWidth + up * halfHeight);
      corners.push_back(center - right * halfWidth + up * halfHeight);
      corners.push_back(center + right * halfWidth - up * halfHeight);
      corners.push_back(center - right * halfWidth - up * halfHeight);
    };
    appendCorners(previousSplit);
    appendCorners(split);

    Vec3f center{};
    for (const auto &corner : corners) {
      center += corner;
    }
    center = center * (1.0f / static_cast<float>(corners.size()));

    float radius = 0.0f;
    for (const auto &corner : corners) {
      radius = std::max(radius, (corner - center).length());
    }
    radius = std::max(radius, 0.5f);

    const Vec3f lightEye = center - lightDir * (radius * 2.0f);
    const Mat4f view = Mat4f::lookAt(lightEye, center, lightUp);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
    for (const auto &corner : corners) {
      const Vec4f p4 = view * Vec4f{corner.x, corner.y, corner.z, 1.0f};
      minX = std::min(minX, p4.x);
      minY = std::min(minY, p4.y);
      minZ = std::min(minZ, p4.z);
      maxX = std::max(maxX, p4.x);
      maxY = std::max(maxY, p4.y);
      maxZ = std::max(maxZ, p4.z);
    }

    const float texelSize =
        (maxX - minX) / std::max(1.0f, m_ubo->param.shadowParams.x);
    if (texelSize > 0.0f) {
      minX = std::floor(minX / texelSize) * texelSize;
      maxX = std::ceil(maxX / texelSize) * texelSize;
      minY = std::floor(minY / texelSize) * texelSize;
      maxY = std::ceil(maxY / texelSize) * texelSize;
    }

    const Mat4f proj = Mat4f::orthographicDepthZeroToOne(
        minX, maxX, minY, maxY, maxZ + radius, minZ - radius);
    m_ubo->param.cascadeViewProj[cascadeIndex] = proj * view;
    setComponent(m_ubo->param.cascadeDepthRanges, cascadeIndex,
                 std::max(0.001f, (maxZ + radius) - (minZ - radius)));
    m_shadowCascadeDebugViews[cascadeIndex] = DirectionalShadowCascadeDebugView{
        .eye = lightEye,
        .target = center,
        .up = lightUp,
        .left = minX,
        .right = maxX,
        .bottom = minY,
        .top = maxY,
        .nearPlane = -(maxZ + radius),
        .farPlane = -(minZ - radius),
    };
    m_shadowCascadeDebugViewValid[cascadeIndex] = true;
    previousSplit = split;
  }

  setActiveShadowCascade(0);
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

std::optional<DirectionalShadowCascadeDebugView>
DirectionalLight::getShadowCascadeDebugView(const u32 cascadeIndex) const {
  if (cascadeIndex >= MaxShadowCascades ||
      !m_shadowCascadeDebugViewValid[cascadeIndex]) {
    return std::nullopt;
  }
  return m_shadowCascadeDebugViews[cascadeIndex];
}

DirectionalLightDataUniquePtr
DirectionalLight::makeShadowCascadeUBOSnapshot(u32 cascadeIndex) const {
  cascadeIndex = std::min(cascadeIndex, getShadowCascadeCount() - 1u);
  auto snapshot = std::make_unique<DirectionalLightData>();
  snapshot->param = m_ubo->param;
  snapshot->param.shadowViewProj = m_ubo->param.cascadeViewProj[cascadeIndex];
  snapshot->setDirty();
  return snapshot;
}

void DirectionalLight::setActiveShadowCascade(u32 cascadeIndex) {
  cascadeIndex = std::min(cascadeIndex, getShadowCascadeCount() - 1u);
  m_ubo->param.shadowViewProj = m_ubo->param.cascadeViewProj[cascadeIndex];
  m_ubo->setDirty();
}

void DirectionalLight::attachToSceneNode(const std::weak_ptr<Scene> &scene,
                                         const std::weak_ptr<SceneNode> &node) {
  m_scene = scene;
  m_node = node;
  if (const auto attached = m_node.lock()) {
    rotateLightNodeToDirection(*attached, m_pendingDirection);
    const Vec3f resolved = getDirection();
    m_ubo->param.dir = Vec4f{resolved.x, resolved.y, resolved.z, 0.0f};
    updateShadowViewProjection();
    m_ubo->setDirty();
  }
}

void DirectionalLight::detachFromSceneNode() {
  m_scene.reset();
  m_node.reset();
}

bool DirectionalLight::supportsPass(const StringID pass) const {
  return m_supportedPasses.find(pass) != m_supportedPasses.end();
}

BoundingBox DirectionalLight::getDebugLocalBounds() const {
  constexpr float radius = 0.16f;
  return BoundingBox{Vec3f{-radius, -radius, -radius},
                     Vec3f{radius, radius, radius}};
}

void DirectionalLight::setSupportedPasses(
    const std::initializer_list<StringID> passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void DirectionalLight::setSupportedPasses(const std::vector<StringID> &passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void DirectionalLight::updateShadowViewProjection() {
  Vec3f dir = getDirection();
  if (std::abs(dir.x) < 1e-5f && std::abs(dir.y) < 1e-5f &&
      std::abs(dir.z) < 1e-5f) {
    dir = Vec3f{0.35f, -1.0f, 0.25f};
  }
  dir = dir.normalized();

  Vec3f up{0.0f, 1.0f, 0.0f};
  if (std::abs(dir.dot(up)) > 0.95f) {
    up = Vec3f{0.0f, 0.0f, 1.0f};
  }

  const Vec3f target{0.0f, 0.0f, 0.0f};
  const Vec3f eye = target - dir * 10.0f;
  const Mat4f view = Mat4f::lookAt(eye, target, up);
  const Mat4f proj = Mat4f::orthographicDepthZeroToOne(-10.0f, 10.0f, -10.0f,
                                                       10.0f, -0.1f, -30.0f);
  m_ubo->param.shadowViewProj = proj * view;
  for (u32 i = 0; i < MaxShadowCascades; ++i) {
    m_ubo->param.cascadeViewProj[i] = m_ubo->param.shadowViewProj;
    setComponent(m_ubo->param.cascadeSplits, i,
                 10.0f * static_cast<float>(i + 1u));
    setComponent(m_ubo->param.cascadeDepthRanges, i, 29.9f);
  }
}

void DirectionalLight::emitLightPropertyChanged() const {
  LX_core::emitLightPropertyChanged(m_scene, m_node);
}

PointLight::PointLight() : m_supportedPasses({Pass_Forward, Pass_Deferred}) {}

std::unique_ptr<LightBase> PointLight::cloneUnique() const {
  auto clone = std::make_unique<PointLight>();
  clone->m_color = m_color;
  clone->m_intensity = m_intensity;
  clone->m_range = m_range;
  clone->m_supportedPasses = m_supportedPasses;
  return clone;
}

Vec3f PointLight::getColor() const { return m_color; }

float PointLight::getIntensity() const { return m_intensity; }

float PointLight::getRange() const { return m_range; }

std::shared_ptr<SceneNode> PointLight::getSceneNode() const {
  return m_node.lock();
}

void PointLight::setColor(const Vec3f &color) {
  m_color = color;
  emitLightPropertyChanged();
}

void PointLight::setIntensity(const float intensity) {
  m_intensity = intensity;
  emitLightPropertyChanged();
}

void PointLight::setRange(const float range) {
  m_range = std::max(0.0f, range);
  emitLightPropertyChanged();
}

void PointLight::attachToSceneNode(const std::weak_ptr<Scene> &scene,
                                   const std::weak_ptr<SceneNode> &node) {
  m_scene = scene;
  m_node = node;
}

void PointLight::detachFromSceneNode() {
  m_scene.reset();
  m_node.reset();
}

bool PointLight::supportsPass(const StringID pass) const {
  return m_supportedPasses.find(pass) != m_supportedPasses.end();
}

BoundingBox PointLight::getDebugLocalBounds() const {
  const float radius = std::max(0.16f, m_range);
  return BoundingBox{Vec3f{-radius, -radius, -radius},
                     Vec3f{radius, radius, radius}};
}

void PointLight::setSupportedPasses(
    const std::initializer_list<StringID> passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void PointLight::setSupportedPasses(const std::vector<StringID> &passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void PointLight::emitLightPropertyChanged() const {
  LX_core::emitLightPropertyChanged(m_scene, m_node);
}

SpotLight::SpotLight() : m_supportedPasses({Pass_Forward, Pass_Deferred}) {}

std::unique_ptr<LightBase> SpotLight::cloneUnique() const {
  auto clone = std::make_unique<SpotLight>();
  clone->m_direction = m_direction;
  clone->m_color = m_color;
  clone->m_intensity = m_intensity;
  clone->m_range = m_range;
  clone->m_innerConeDegrees = m_innerConeDegrees;
  clone->m_outerConeDegrees = m_outerConeDegrees;
  clone->m_supportedPasses = m_supportedPasses;
  return clone;
}

Vec3f SpotLight::getDirection() const {
  if (const auto node = m_node.lock()) {
    return lightDirectionFromNode(*node);
  }
  return Vec3f{0.0f, 0.0f, -1.0f};
}

Vec3f SpotLight::getColor() const { return m_color; }

float SpotLight::getIntensity() const { return m_intensity; }

float SpotLight::getRange() const { return m_range; }

float SpotLight::getInnerConeDegrees() const { return m_innerConeDegrees; }

float SpotLight::getOuterConeDegrees() const { return m_outerConeDegrees; }

std::shared_ptr<SceneNode> SpotLight::getSceneNode() const {
  return m_node.lock();
}

void SpotLight::setDirection(const Vec3f &direction) {
  m_direction = direction;
  if (const auto node = m_node.lock()) {
    rotateLightNodeToDirection(*node, direction);
  }
  emitLightPropertyChanged();
}

void SpotLight::setColor(const Vec3f &color) {
  m_color = color;
  emitLightPropertyChanged();
}

void SpotLight::setIntensity(const float intensity) {
  m_intensity = intensity;
  emitLightPropertyChanged();
}

void SpotLight::setRange(const float range) {
  m_range = std::max(0.0f, range);
  emitLightPropertyChanged();
}

void SpotLight::setInnerConeDegrees(const float degrees) {
  m_innerConeDegrees = std::max(0.0f, degrees);
  if (m_outerConeDegrees < m_innerConeDegrees) {
    m_outerConeDegrees = m_innerConeDegrees;
  }
  emitLightPropertyChanged();
}

void SpotLight::setOuterConeDegrees(const float degrees) {
  m_outerConeDegrees = std::max(m_innerConeDegrees, degrees);
  emitLightPropertyChanged();
}

void SpotLight::attachToSceneNode(const std::weak_ptr<Scene> &scene,
                                  const std::weak_ptr<SceneNode> &node) {
  m_scene = scene;
  m_node = node;
  if (const auto attached = m_node.lock()) {
    rotateLightNodeToDirection(*attached, m_direction);
  }
}

void SpotLight::detachFromSceneNode() {
  m_scene.reset();
  m_node.reset();
}

bool SpotLight::supportsPass(const StringID pass) const {
  return m_supportedPasses.find(pass) != m_supportedPasses.end();
}

BoundingBox SpotLight::getDebugLocalBounds() const {
  const float radius = std::max(0.16f, m_range);
  return BoundingBox{Vec3f{-radius, -radius, -radius},
                     Vec3f{radius, radius, radius}};
}

void SpotLight::setSupportedPasses(
    const std::initializer_list<StringID> passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void SpotLight::setSupportedPasses(const std::vector<StringID> &passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void SpotLight::emitLightPropertyChanged() const {
  LX_core::emitLightPropertyChanged(m_scene, m_node);
}

} // namespace LX_core
