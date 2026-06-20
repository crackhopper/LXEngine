#pragma once

#include "core/resource/resource_uri.hpp"

#include <optional>

namespace LX_core {

struct SceneIblBakeMarker final {
  bool enabled = false;
};

struct SceneEnvironmentNode final {
  ResourceUri featureUri;
  SceneIblBakeMarker bake;
};

enum class SceneSkyboxMode {
  Finite,
  Infinite,
};

struct SceneSkyboxNode final {
  SceneSkyboxMode mode = SceneSkyboxMode::Finite;
  ResourceUri featureUri;
  SceneIblBakeMarker bake;
};

struct SceneNodeBakeMarkers final {
  std::optional<SceneIblBakeMarker> ibl;
};

void validateSceneIblBakeMarker(const SceneIblBakeMarker &marker,
                                const char *fieldName);
void validateSceneEnvironmentNode(const SceneEnvironmentNode &environment,
                                  const char *fieldName);
void validateSceneSkyboxNode(const SceneSkyboxNode &skybox,
                             const char *fieldName);

} // namespace LX_core
