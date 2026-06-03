#pragma once

#include "core/offline/offline_render_job.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

namespace LX_core::offline {

void validateOfflineUploadView(const SceneResourceTableUploadView &uploadView);
void validateOfflineRenderJob(const OfflineRenderJob &job);

} // namespace LX_core::offline
