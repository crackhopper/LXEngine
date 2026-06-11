#pragma once

#include "core/asset/material_instance.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace LX_core {
class SceneResourceTable;
}

namespace LX_infra {

struct GenericMaterialLoadOptions final {
  std::optional<bool> forceIbl;
  std::optional<bool> alphaTransparency;
  std::optional<std::string> technique;
};

/// Load a material from a YAML material definition file (.material).
/// Only material v2 files are accepted. They are parsed as PBRT BSDF envelope
/// contracts without shader compilation or material-local technique data.
LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const std::filesystem::path &materialPath,
                    const GenericMaterialLoadOptions &options = {});

LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const std::filesystem::path &materialPath,
                    LX_core::SceneResourceTable &resourceTable,
                    const GenericMaterialLoadOptions &options = {});

} // namespace LX_infra
