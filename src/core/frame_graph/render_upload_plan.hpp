#pragma once

#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"

#include <vector>

namespace LX_core {

struct RenderUploadPlan final {
  RenderDomain domain = RenderDomain::Realtime;
  std::vector<GpuResourceRef> resources;
};

[[nodiscard]] RenderUploadPlan
buildRenderUploadPlan(const RenderWorkQueue &queue);

} // namespace LX_core
