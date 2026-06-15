#pragma once

#include "core/resource/resource_handle.hpp"
#include "core/resource/resource_uri.hpp"

#include <string>
#include <vector>

namespace LX_core {

enum class SceneResourceType {
  Unknown,
  Mesh,
  Texture,
  Material,
  MaterialHeader,
  Spectrum,
  BsdfTable,
  Camera,
  Light,
  Renderer,
  RenderPathGraph,
  RenderFeature,
  Shader,
  RenderEffect,
};

enum class ResourceState {
  Unloaded,
  Loading,
  Ready,
  Failed,
  Dirty,

  Empty = Unloaded,
  Alive = Ready,
  Error = Failed,
};

using ResourceMetadataState = ResourceState;

struct ResourceDiagnostic final {
  ResourceUri ownerUri;
  ResourceUri resourceUri;
  std::string parserName;
  std::string message;
};

struct ResourceMetadata final {
  SceneResourceType type = SceneResourceType::Unknown;
  ResourceUri uri;
  ResourceState state = ResourceState::Ready;
  u64 generation = 0;
  u64 version = 0;
  std::vector<ResourceUri> dependencies;
  std::vector<ResourceIdentityHandle> dependencyHandles;
  std::vector<ResourceIdentityHandle> dependents;
  std::vector<ResourceDiagnostic> diagnostics;
  std::string contentHash;
};

} // namespace LX_core
