#pragma once

#include "core/platform/types.hpp"

namespace LX_core::image {

enum class ToneMappingMode { Aces, Reinhard };

struct ToneMappingSettings final {
  float exposure = 1.0f;
  float gamma = 2.2f;
  ToneMappingMode mode = ToneMappingMode::Aces;
};

[[nodiscard]] float toneMapLinear(float value,
                                  const ToneMappingSettings &settings);
[[nodiscard]] u8 toneMapLinearToSrgb8(float value,
                                      const ToneMappingSettings &settings);
[[nodiscard]] const char *toneMappingModeName(ToneMappingMode mode);

} // namespace LX_core::image
