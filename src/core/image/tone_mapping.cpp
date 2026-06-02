#include "core/image/tone_mapping.hpp"

#include <algorithm>
#include <cmath>

namespace LX_core::image {

float toneMapLinear(float value, const ToneMappingSettings &settings) {
  if (!std::isfinite(value)) {
    return 0.0f;
  }

  const float exposure = std::isfinite(settings.exposure) ? settings.exposure : 0.0f;
  const float exposed = std::max(0.0f, value * std::max(0.0f, exposure));
  if (settings.mode == ToneMappingMode::Reinhard) {
    return exposed / (exposed + 1.0f);
  }

  constexpr float a = 2.51f;
  constexpr float b = 0.03f;
  constexpr float c = 2.43f;
  constexpr float d = 0.59f;
  constexpr float e = 0.14f;
  return std::clamp((exposed * (a * exposed + b)) /
                        (exposed * (c * exposed + d) + e),
                    0.0f, 1.0f);
}

u8 toneMapLinearToSrgb8(float value, const ToneMappingSettings &settings) {
  const float mapped = toneMapLinear(value, settings);
  const float gamma =
      std::isfinite(settings.gamma) ? std::max(settings.gamma, 0.0001f) : 0.0001f;
  const float srgb = std::pow(std::clamp(mapped, 0.0f, 1.0f), 1.0f / gamma);
  return static_cast<u8>(std::round(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

const char *toneMappingModeName(ToneMappingMode mode) {
  switch (mode) {
  case ToneMappingMode::Aces:
    return "aces";
  case ToneMappingMode::Reinhard:
    return "reinhard";
  }
  return "aces";
}

} // namespace LX_core::image
