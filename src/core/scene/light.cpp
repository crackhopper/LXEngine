#include "light.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {

DirectionalLight::DirectionalLight()
    : ubo(std::make_shared<DirectionalLightData>()),
      m_supportedPasses({Pass_Forward, Pass_Deferred}) {}

Vec3f DirectionalLight::getDirection() const {
  return Vec3f{ubo->param.dir.x, ubo->param.dir.y, ubo->param.dir.z};
}

Vec3f DirectionalLight::getColor() const {
  return Vec3f{ubo->param.color.x, ubo->param.color.y, ubo->param.color.z};
}

float DirectionalLight::getIntensity() const {
  return ubo->param.color.w;
}

void DirectionalLight::setDirection(const Vec3f &direction) {
  ubo->param.dir = Vec4f{direction.x, direction.y, direction.z, 0.0f};
  ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setColor(const Vec3f &color) {
  ubo->param.color.x = color.x;
  ubo->param.color.y = color.y;
  ubo->param.color.z = color.z;
  ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setIntensity(const float intensity) {
  ubo->param.color.w = intensity;
  ubo->setDirty();
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
