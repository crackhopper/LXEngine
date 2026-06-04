#pragma once

#include "core/asset/shader.hpp"
#include <string_view>

namespace LX_core::backend::offline {

[[nodiscard]] LX_core::IShaderSharedPtr createOfflineComputeShader(
    std::string_view shaderName);

} // namespace LX_core::backend::offline
