#pragma once

#include "core/offline/offline_render_job.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <vector>

namespace LX_core::offline {

struct OfflineSceneStorageResources final {
  std::vector<IGpuResourceSharedPtr> descriptorResources;
  IGpuResourceSharedPtr outputPixels;
};

[[nodiscard]] OfflineSceneStorageResources
buildOfflineSceneStorageResources(const OfflineRenderJob &job);

} // namespace LX_core::offline
