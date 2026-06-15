#pragma once

#include "core/platform/types.hpp"

namespace LX_core {

struct ResourceIdentityHandle final {
  u32 index = 0;
  u32 generation = 0;

  [[nodiscard]] bool isValid() const { return generation != 0; }
  bool operator==(const ResourceIdentityHandle &other) const = default;
};

} // namespace LX_core
