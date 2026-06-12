#pragma once

#include "core/asset/material_pass_definition.hpp"

namespace LX_core {

enum class RenderPassStage {
  Raster,
  Compute,
};

enum class RenderPassDispatch {
  Draw,
  Fullscreen,
  Compute,
};

} // namespace LX_core
