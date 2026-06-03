#pragma once

#include "core/frame_graph/frame_graph.hpp"
#include "core/offline/offline_render_profile.hpp"

namespace LX_core::offline {

[[nodiscard]] FrameGraph createOfflineRenderFrameGraph(const OutputProfile &output);

} // namespace LX_core::offline
