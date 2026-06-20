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

void validateSceneSkyboxNode(const SceneSkyboxNode &skybox,
                             const char *fieldName) {
  if (fieldName == nullptr || std::string(fieldName).empty()) {
    throw std::runtime_error("scene skybox node field path is empty");
  }
  if (skybox.mode == SceneSkyboxMode::Finite) {
    if (skybox.meshUri.empty()) {
      throw std::runtime_error(std::string(fieldName) +
                               ".mesh.uri must be non-empty for finite "
                               "skybox");
    }
    if (skybox.materialUri.empty()) {
      throw std::runtime_error(std::string(fieldName) +
                               ".material.uri must be non-empty for finite "
                               "skybox");
    }
    if (!skybox.featureUri.empty()) {
      throw std::runtime_error(std::string(fieldName) +
                               ".feature.uri is only supported for infinite "
                               "skybox");
    }
    return;
  }
  if (skybox.mode == SceneSkyboxMode::Infinite) {
    if (skybox.featureUri.empty()) {
      throw std::runtime_error(std::string(fieldName) +
                               ".feature.uri must be non-empty for infinite "
                               "skybox");
    }
    if (!skybox.meshUri.empty() || !skybox.materialUri.empty()) {
      throw std::runtime_error(std::string(fieldName) +
                               ".mesh/material are only supported for finite "
                               "skybox");
    }
    validateSceneIblBakeMarker(skybox.bake,
                               (std::string(fieldName) + ".bake").c_str());
  }
}

} // namespace LX_core
