#pragma once

#include "core/offline/offline_render_job.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"

#include <vector>

namespace LX_core::offline {

struct OfflineSceneStorageResources final {
  DescriptorResourceList descriptorResources;
  GpuResourceRef outputPixels;
};

[[nodiscard]] OfflineSceneStorageResources
buildOfflineSceneStorageResources(OfflineRenderJob &job);

} // namespace LX_core::offline
