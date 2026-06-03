#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {

struct GltfSceneAssetLoadResult final {
  LX_core::MeshSharedPtr mesh;
  LX_core::MaterialInstanceSharedPtr material;
  bool generatedTangents = false;
  bool normalMapEnabled = false;
  std::vector<std::string> warnings;
};

[[nodiscard]] GltfSceneAssetLoadResult
loadGltfSceneAsset(const std::filesystem::path &gltfPath);

} // namespace LX_infra::scene_asset
