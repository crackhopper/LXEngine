#include "light.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {

DirectionalLight::DirectionalLight()
    : m_ubo(std::make_shared<DirectionalLightData>()),
      m_supportedPasses({Pass_Forward, Pass_Deferred}) {}

Vec3f DirectionalLight::getDirection() const {
  return Vec3f{m_ubo->param.dir.x, m_ubo->param.dir.y, m_ubo->param.dir.z};
}

Vec3f DirectionalLight::getColor() const {
  return Vec3f{m_ubo->param.color.x, m_ubo->param.color.y, m_ubo->param.color.z};
}

float DirectionalLight::getIntensity() const {
  return m_ubo->param.color.w;
}

std::shared_ptr<SceneNode> DirectionalLight::getSceneNode() const {
  return m_node.lock();
}

void DirectionalLight::setDirection(const Vec3f &direction) {
  m_ubo->param.dir = Vec4f{direction.x, direction.y, direction.z, 0.0f};
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

void DirectionalLight::emitLightPropertyChanged() const {
  const auto scene = m_scene.lock();
  const auto node = m_node.lock();
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

} // namespace LX_core
