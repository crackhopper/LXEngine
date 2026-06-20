#pragma once

#include "core/offline/offline_render_profile.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

namespace LX_core::offline {

void validateOfflineUploadView(const SceneResourceTableUploadView &uploadView);
void validateOfflineRenderInputs(const SceneResourceTable &scene,
                                 const OutputProfile &output);

} // namespace LX_core::offline
