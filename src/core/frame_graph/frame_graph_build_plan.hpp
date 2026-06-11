#pragma once

#include "core/asset/material_technique_set.hpp"
#include "core/asset/render_effect.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/utils/string_table.hpp"
#include <vector>

namespace LX_core {

class GraphResourceRegistry;

struct FrameGraphEffectInput {
  MaterialTechnique technique;
};

struct FrameGraphMaterialTechniqueInput {
  MaterialTechnique technique;
};

struct FrameGraphBuildPlanInput {
  std::vector<FrameGraphEffectInput> preEffects;
  std::vector<FrameGraphMaterialTechniqueInput> materialTechniques;
  std::vector<FrameGraphEffectInput> postEffects;
};

[[nodiscard]] FrameGraph buildFrameGraphFromSourceTargetContracts(
    const FrameGraphBuildPlanInput &input,
    const GraphResourceRegistry &registry);

[[nodiscard]] FrameGraph
buildFrameGraphFromRenderPathGraph(const RenderPathGraph &graph,
                                   const GraphResourceRegistry &registry);

void validateRenderPathGraphPassSet(const RenderPathGraph &graph,
                                    const std::vector<StringID> &requiredPasses,
                                    const std::vector<StringID> &supportedPasses);

} // namespace LX_core
