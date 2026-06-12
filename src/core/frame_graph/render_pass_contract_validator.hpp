#pragma once

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct RenderPassContractValidationReport final {
  std::vector<std::string> diagnostics;
  [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

[[nodiscard]] RenderPassContractValidationReport
validateRenderPassContractResources(const RenderPathGraph &graph,
                                    const GraphResourceRegistry &registry);

} // namespace LX_core
