#include "scene.hpp"

namespace LX_core {

std::vector<IRenderResourcePtr> Scene::getSceneLevelResources() const {
  std::vector<IRenderResourcePtr> out;
  if (camera) {
    if (auto camUbo = camera->getUBO()) {
      out.push_back(std::dynamic_pointer_cast<IRenderResource>(camUbo));
    }
  }
  if (directionalLight) {
    if (auto lightUbo = directionalLight->getUBO()) {
      out.push_back(std::dynamic_pointer_cast<IRenderResource>(lightUbo));
    }
  }
  return out;
}

} // namespace LX_core
