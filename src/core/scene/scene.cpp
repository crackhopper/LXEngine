#include "scene.hpp"

namespace LX_core {

Scene::~Scene() {
  for (const auto &renderable : m_renderables) {
    auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node)
      continue;
    node->attachToScene(nullptr);
  }
}

void Scene::revalidateNodesUsing(const MaterialInstanceSharedPtr &materialInstance) {
  if (!materialInstance)
    return;
  for (const auto &renderable : m_renderables) {
    auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node)
      continue;
    if (node->getMaterialInstance() != materialInstance)
      continue;
    node->rebuildValidatedCache();
  }
}

std::vector<IGpuResourceSharedPtr>
Scene::getSceneLevelResources(StringID pass, const RenderTarget &target) const {
  std::vector<IGpuResourceSharedPtr> out;

  // Cameras filter by target only. A camera draws to one target; whether a
  // pass draws to that target is orthogonal to the camera's identity.
  for (const auto &cam : m_cameras) {
    if (!cam)
      continue;
    if (!cam->matchesTarget(target))
      continue;
    if (auto camUbo = cam->getUBO()) {
      out.push_back(std::dynamic_pointer_cast<IGpuResource>(camUbo));
    }
  }

  // Lights filter by pass only. A light's target scope is transitive — it
  // illuminates any surface being drawn in a pass it participates in.
  for (const auto &light : m_lights) {
    if (!light)
      continue;
    if (!light->supportsPass(pass))
      continue;
    if (auto lightUbo = light->getUBO()) {
      out.push_back(lightUbo);
    }
  }

  return out;
}

} // namespace LX_core
