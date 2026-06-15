#pragma once

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/utils/string_table.hpp"
#include <vector>

namespace LX_core {

class GraphResourceRegistry;

[[nodiscard]] FrameGraph
buildFrameGraphFromRenderPathGraph(const RenderPathGraph &graph,
                                   const GraphResourceRegistry &registry);

void validateRenderPathGraphPassSet(const RenderPathGraph &graph,
                                    const std::vector<StringID> &requiredPasses,
                                    const std::vector<StringID> &supportedPasses);

} // namespace LX_core
