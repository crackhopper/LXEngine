#pragma once

#include "core/resource/resource_metadata.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

struct ScenePackageResourceRecord final {
  ResourceIdentityHandle sourceHandle;
  ResourceMetadata metadata;
};

struct ScenePackageManifest final {
  u32 schemaVersion = 1;
  std::vector<ScenePackageResourceRecord> resources;
  std::string rootHash;
};

[[nodiscard]] ScenePackageManifest
buildScenePackageManifest(const SceneResourceGraphExport &graph);

[[nodiscard]] std::string
computeScenePackageRootHash(const ScenePackageManifest &manifest);

[[nodiscard]] std::string
writeScenePackageManifest(const ScenePackageManifest &manifest);

[[nodiscard]] ScenePackageManifest
readScenePackageManifest(std::string_view manifestText);

[[nodiscard]] std::vector<std::byte>
writeScenePackageBytes(const ScenePackageManifest &manifest);

[[nodiscard]] ScenePackageManifest
readScenePackageBytes(std::span<const std::byte> bytes);

} // namespace LX_core
