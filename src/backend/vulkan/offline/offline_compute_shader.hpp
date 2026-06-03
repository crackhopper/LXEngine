#pragma once

#include "core/asset/shader.hpp"

namespace LX_core::backend::offline {

[[nodiscard]] LX_core::IShaderSharedPtr createOfflinePrimaryRayShader();

} // namespace LX_core::backend::offline
