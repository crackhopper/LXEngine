#pragma once

#include "core/frame_graph/frame_graph.hpp"
#include "core/offline/offline_render_job.hpp"

namespace LX_core::offline {

[[nodiscard]] FrameGraph buildOfflineRenderWorkGraph(const OfflineRenderJob &job);

} // namespace LX_core::offline
