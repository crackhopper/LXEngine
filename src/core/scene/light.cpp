#include "light.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cmath>

namespace LX_core {
namespace {

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
  m_ubo->param.shadowParams = Vec4f{1024.0f, 0.004f, 0.45f, 0.0f};
  updateShadowViewProjection();
}

Vec3f DirectionalLight::getDirection() const {
  return Vec3f{m_ubo->param.dir.x, m_ubo->param.dir.y, m_ubo->param.dir.z};
}

Vec3f DirectionalLight::getColor() const {
  return Vec3f{m_ubo->param.color.x, m_ubo->param.color.y, m_ubo->param.color.z};
}

float DirectionalLight::getIntensity() const {
  return m_ubo->param.color.w;
}

Mat4f DirectionalLight::getShadowViewProj() const {
  return m_ubo->param.shadowViewProj;
}

Vec4f DirectionalLight::getShadowParams() const {
  return m_ubo->param.shadowParams;
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
  const Mat4f proj = Mat4f::orthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f,
                                         30.0f);
  m_ubo->param.shadowViewProj = proj * view;
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

void PointLight::setSupportedPasses(const std::initializer_list<StringID> passes) {
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

void SpotLight::setSupportedPasses(const std::initializer_list<StringID> passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void SpotLight::setSupportedPasses(const std::vector<StringID> &passes) {
  m_supportedPasses = {passes.begin(), passes.end()};
}

void SpotLight::emitLightPropertyChanged() const {
  LX_core::emitLightPropertyChanged(m_scene, m_node);
}

} // namespace LX_core
