#pragma once

#include "core/asset/material_technique_set.hpp"

#include <optional>
#include <string>
#include <vector>

namespace LX_core {

enum class RenderEffectPhase {
  Pre,
  Post,
};

struct RenderEffect final {
  std::string name;
  RenderEffectPhase phase = RenderEffectPhase::Post;
  MaterialTechnique technique;
};

} // namespace LX_core
