#pragma once

#include "core/asset/material_contract.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

struct MaterialContractReflectionResult final {
  std::optional<LX_core::MaterialContractReflection> reflection;
  std::vector<std::string> diagnostics;
};

struct MaterialContractReflectionSetValidationResult final {
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialContractReflectionResult
reflectMaterialContractSource(const LX_core::ResourceUri &sourceUri,
                              std::string_view sourceText);

[[nodiscard]] MaterialContractReflectionSetValidationResult
validateMaterialContractReflectionSet(
    const std::vector<LX_core::MaterialContractReflection> &reflections);

} // namespace LX_infra
