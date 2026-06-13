#pragma once

#include "core/asset/material_contract.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

struct MaterialContractReflectionResult final {
  std::optional<LX_core::MaterialContractReflection> reflection;
  std::vector<std::string> diagnostics;
};

struct MaterialContractSourceLoadResult final {
  std::optional<std::string> sourceText;
  std::vector<std::string> diagnostics;
};

struct MaterialContractReflectionSetValidationResult final {
  std::vector<std::string> diagnostics;
};

using MaterialContractReflector =
    std::function<MaterialContractReflectionResult(
        const LX_core::ResourceUri &, std::string_view)>;
using MaterialContractSourceLoader =
    std::function<MaterialContractSourceLoadResult(
        const LX_core::ResourceUri &)>;

[[nodiscard]] MaterialContractReflectionResult
reflectMaterialContractSource(const LX_core::ResourceUri &sourceUri,
                              std::string_view sourceText);

[[nodiscard]] MaterialContractSourceLoadResult
loadMaterialContractSourceText(const LX_core::ResourceUri &sourceUri);

[[nodiscard]] MaterialContractReflectionResult
loadAndReflectMaterialContractSource(
    const LX_core::ResourceUri &sourceUri,
    const MaterialContractReflector &reflector = reflectMaterialContractSource,
    const MaterialContractSourceLoader &sourceLoader =
        loadMaterialContractSourceText);

[[nodiscard]] MaterialContractReflectionSetValidationResult
validateMaterialContractReflectionSet(
    const std::vector<LX_core::MaterialContractReflection> &reflections);

} // namespace LX_infra
