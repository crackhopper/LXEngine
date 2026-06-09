#pragma once

#include "core/asset/mesh.hpp"

#include <filesystem>

namespace LX_infra::scene_asset {

[[nodiscard]] LX_core::MeshSharedPtr
loadSceneMeshAsset(const std::filesystem::path &meshPath);

} // namespace LX_infra::scene_asset
