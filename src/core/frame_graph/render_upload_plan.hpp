#pragma once

#include "core/frame_graph/render_input.hpp"
#include "core/scene/scene.hpp"

#include <memory>
#include <vector>

namespace LX_core {

struct RenderUploadPlan final {
  RenderDomain domain = RenderDomain::Realtime;
  std::vector<GpuResourceRef> resources;
};

[[nodiscard]] RenderUploadPlan
buildRenderUploadPlan(const std::vector<std::unique_ptr<RenderInput>> &inputs,
                      const std::vector<RenderInputDesc> &descs);

} // namespace LX_core
