#pragma once

#include "core/asset/material_technique_set.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct TechniqueValidationReport final {
  std::vector<std::string> diagnostics;
  [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

[[nodiscard]] TechniqueValidationReport
validateTechniqueResources(const MaterialTechnique &technique,
                           const GraphResourceRegistry &registry);

} // namespace LX_core
