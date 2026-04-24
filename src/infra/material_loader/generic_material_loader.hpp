#pragma once

#include "core/asset/material_instance.hpp"

#include <filesystem>

namespace LX_infra {

/// Load a material from a YAML material definition file (.material).
/// The file describes shader(s), variants, canonical default parameters,
/// canonical default resources, and per-pass shader/render-state structure.
/// Each pass can optionally specify its own shader, but runtime parameter
/// values remain instance-global. No material-type-specific C++ code is needed.
LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const std::filesystem::path &materialPath);

} // namespace LX_infra
