#pragma once

#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/object.hpp"

#include <vector>

namespace LX_core {

struct RenderUploadPlan final {
  RenderDomain domain = RenderDomain::Realtime;
  std::vector<IGpuResourceSharedPtr> resources;
  std::vector<PerDrawDataSharedPtr> pushConstants;
};

[[nodiscard]] RenderUploadPlan
buildRenderUploadPlan(const RenderWorkQueue &queue);

} // namespace LX_core
