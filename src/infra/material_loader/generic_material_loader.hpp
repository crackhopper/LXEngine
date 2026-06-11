#pragma once

#include "core/asset/material_instance.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace LX_infra {

struct GenericMaterialLoadOptions final {
  std::optional<bool> forceIbl;
  std::optional<bool> alphaTransparency;
  std::optional<std::string> technique;
};

/// Load a material from a YAML material definition file (.material).
/// Material v2 files are parsed as PBRT BSDF envelope contracts without shader
/// compilation. Legacy/custom files describe shader(s), variants, canonical
/// default parameters/resources, and per-pass shader/render-state structure.
LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const std::filesystem::path &materialPath,
                    const GenericMaterialLoadOptions &options = {});

} // namespace LX_infra
