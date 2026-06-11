#pragma once

#include "core/asset/material_technique_set.hpp"
#include "core/asset/render_effect.hpp"
#include "core/frame_graph/frame_graph.hpp"
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

} // namespace LX_core
