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
    : m_ubo(std::make_shared<DirectionalLightData>()),
      m_supportedPasses({Pass_Forward, Pass_Deferred, Pass_Shadow}) {
  m_ubo->param.dir = Vec4f{0.35f, -1.0f, 0.25f, 0.0f};
  m_ubo->param.color = Vec4f{1.0f, 0.98f, 0.9f, 1.0f};
  m_ubo->param.shadowParams =
      Vec4f{1024.0f, 0.004f, 0.45f, static_cast<float>(MaxShadowCascades)};
  updateShadowViewProjection();
}

Vec3f DirectionalLight::getDirection() const {
  return Vec3f{m_ubo->param.dir.x, m_ubo->param.dir.y, m_ubo->param.dir.z};
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
  m_ubo->param.dir = Vec4f{direction.x, direction.y, direction.z, 0.0f};
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
      m_ubo->param.cascadeViewProj[cascadeIndex] =
          m_ubo->param.cascadeViewProj[cascadeCount - 1u];
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
        minX, maxX, minY, maxY, minZ - radius, maxZ + radius);
    m_ubo->param.cascadeViewProj[cascadeIndex] = proj * view;
    previousSplit = split;
  }

  setActiveShadowCascade(0);
  m_ubo->setDirty();
  emitLightPropertyChanged();
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
  const Mat4f proj = Mat4f::orthographicDepthZeroToOne(
      -10.0f, 10.0f, -10.0f, 10.0f, -30.0f, -0.1f);
  m_ubo->param.shadowViewProj = proj * view;
  for (u32 i = 0; i < MaxShadowCascades; ++i) {
    m_ubo->param.cascadeViewProj[i] = m_ubo->param.shadowViewProj;
    setComponent(m_ubo->param.cascadeSplits, i,
                 10.0f * static_cast<float>(i + 1u));
  }
}

void DirectionalLight::emitLightPropertyChanged() const {
  LX_core::emitLightPropertyChanged(m_scene, m_node);
}

PointLight::PointLight() : m_supportedPasses({Pass_Forward, Pass_Deferred}) {}

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

Vec3f SpotLight::getDirection() const { return m_direction; }

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
