#pragma once

#include "core/asset/texture.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace LX_core {

class AudioSpectrumTexture final {
public:
  explicit AudioSpectrumTexture(StringID bindingName = StringID("iChannel0"),
                                u32 width = 512)
      : m_sampler(
            std::make_shared<CombinedTextureSampler>(std::make_shared<Texture>(
                TextureDesc{width, 2, TextureFormat::RGBA8,
                            TextureContent::Data},
                makeFakePixels(width, 0.0f)))) {
    m_sampler->setBindingName(bindingName);
  }

  [[nodiscard]] CombinedTextureSamplerSharedPtr sampler() const {
    return m_sampler;
  }

  void updateFake(float timeSeconds) {
    m_sampler->update(
        makeFakePixels(m_sampler->texture()->desc().width, timeSeconds));
  }

  [[nodiscard]] static std::vector<u8> makeFakePixels(u32 width,
                                                      float timeSeconds) {
    std::vector<u8> pixels(static_cast<usize>(width) * 2U * 4U, 0);
    const u32 safeWidth = std::max(width, 1U);
    for (u32 x = 0; x < width; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(safeWidth);
      const float bass = 0.5f + 0.5f * std::sin(timeSeconds * 1.7f + u * 8.0f);
      const float mid = 0.5f + 0.5f * std::sin(timeSeconds * 2.3f + u * 19.0f);
      const float high = 0.5f + 0.5f * std::sin(timeSeconds * 3.9f + u * 37.0f);
      writePixel(pixels, width, x, 0, bass, mid, high, 1.0f);

      const float wave = 0.5f + 0.5f * std::sin(timeSeconds * 5.0f + u * 50.0f);
      writePixel(pixels, width, x, 1, wave, wave, wave, 1.0f);
    }
    return pixels;
  }

private:
  static void writePixel(std::vector<u8> &pixels, u32 width, u32 x, u32 y,
                         float r, float g, float b, float a) {
    const usize offset = (static_cast<usize>(y) * width + x) * 4U;
    pixels[offset + 0] = toByte(r);
    pixels[offset + 1] = toByte(g);
    pixels[offset + 2] = toByte(b);
    pixels[offset + 3] = toByte(a);
  }

  static u8 toByte(float value) {
    return static_cast<u8>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
  }

  CombinedTextureSamplerSharedPtr m_sampler;
};

} // namespace LX_core
