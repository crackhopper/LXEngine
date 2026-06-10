#pragma once

#include "core/resource/resource_handle.hpp"
#include "core/resource/resource_uri.hpp"

#include <string>
#include <vector>

namespace LX_core {

enum class SceneResourceType {
  Mesh,
  Texture,
  Material,
  Camera,
  Light,
  RenderEffect,
};

enum class ResourceMetadataState {
  Empty,
  Alive,
  Error,
};

struct ResourceDiagnostic final {
  ResourceUri ownerUri;
  ResourceUri resourceUri;
  std::string parserName;
  std::string message;
};

struct ResourceMetadata final {
  SceneResourceType type = SceneResourceType::Material;
  ResourceUri uri;
  ResourceMetadataState state = ResourceMetadataState::Alive;
  std::vector<ResourceUri> dependencies;
  std::vector<ResourceDiagnostic> diagnostics;
  std::string contentHash;
};

} // namespace LX_core
