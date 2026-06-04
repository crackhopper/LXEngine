#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {

struct GltfMeshAssetLoadResult final {
  LX_core::MeshSharedPtr mesh;
  bool generatedTangents = false;
  std::vector<std::string> warnings;
};

struct GltfSceneAssetLoadResult final {
  LX_core::MeshSharedPtr mesh;
  LX_core::MaterialInstanceSharedPtr material;
  bool generatedTangents = false;
  bool normalMapEnabled = false;
  std::vector<std::string> warnings;
};

[[nodiscard]] GltfMeshAssetLoadResult
loadGltfMeshAsset(const std::filesystem::path &gltfPath);

[[nodiscard]] GltfSceneAssetLoadResult
loadGltfSceneAsset(const std::filesystem::path &gltfPath);

[[nodiscard]] GltfSceneAssetLoadResult
loadGltfSceneAsset(const std::filesystem::path &gltfPath,
                   const std::filesystem::path &pbrMaterialUri);

} // namespace LX_infra::scene_asset
