#include "core/scene/scene_environment_node.hpp"

#include <stdexcept>
#include <string>

namespace LX_core {

void validateSceneIblBakeMarker(const SceneIblBakeMarker &,
                                const char *fieldName) {
  if (fieldName == nullptr || std::string(fieldName).empty()) {
    throw std::runtime_error("scene IBL bake marker field path is empty");
  }
}

void validateSceneEnvironmentNode(const SceneEnvironmentNode &environment,
                                  const char *fieldName) {
  if (environment.featureUri.empty()) {
    throw std::runtime_error(std::string(fieldName) +
                             ".feature.uri must be non-empty");
  }
  validateSceneIblBakeMarker(environment.bake,
                             (std::string(fieldName) + ".bake").c_str());
}

} // namespace LX_core
