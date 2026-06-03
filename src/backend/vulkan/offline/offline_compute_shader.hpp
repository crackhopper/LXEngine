#pragma once

#include "core/asset/shader.hpp"
#include "core/offline/offline_render_profile.hpp"

namespace LX_core::backend::offline {

[[nodiscard]] LX_core::IShaderSharedPtr createOfflineComputeShader(
    LX_core::offline::OfflineShaderMode mode);

} // namespace LX_core::backend::offline
