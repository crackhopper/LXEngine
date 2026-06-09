#pragma once

#include "core/asset/material_instance.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {

using SceneMaterialPathResolver =
    std::function<std::filesystem::path(const std::string &)>;
using GenericMaterialLoader =
    std::function<LX_core::MaterialInstanceSharedPtr(
        const std::filesystem::path &)>;

struct SceneMaterialBindingLoadDesc final {
  std::optional<std::string> meshUri;
  scene_io::MaterialBindingDocument binding;
  scene_io::MaterialOverrideState materialOverrides;
  scene_io::MaterialOverrideState nodeMaterialOverrides;
  SceneMaterialPathResolver resolveAssetPath;
  GenericMaterialLoader loadGenericMaterial;
};

void applySceneMaterialOverrides(
    const LX_core::MaterialInstanceSharedPtr &material,
    const scene_io::MaterialOverrideState &overrides);

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadSceneMaterialBinding(const SceneMaterialBindingLoadDesc &desc);

} // namespace LX_infra::scene_asset
