#pragma once

#include "core/asset/material_technique_set.hpp"

#include <optional>
#include <string>
#include <vector>

namespace YAML {
class Node;
}

namespace LX_infra {

struct MaterialPassContractParseResult final {
  std::optional<LX_core::MaterialPassContract> pass;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialPassContractParseResult parseMaterialPassContract(
    const std::string &passName, const YAML::Node &node,
    const std::string &fieldPrefix);

} // namespace LX_infra
