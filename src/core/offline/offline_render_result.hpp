#pragma once

#include "core/platform/types.hpp"

#include <vector>

namespace LX_core::offline {

struct OfflineReadbackImage final {
  u32 width = 0;
  u32 height = 0;
  std::vector<float> rgba;

  [[nodiscard]] usize pixelCount() const {
    return static_cast<usize>(width) * static_cast<usize>(height);
  }
};

} // namespace LX_core::offline
