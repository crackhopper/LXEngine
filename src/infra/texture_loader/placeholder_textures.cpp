#include "placeholder_textures.hpp"

namespace LX_infra {

namespace {

LX_core::CombinedTextureSamplerSharedPtr makePlaceholder(uint8_t r, uint8_t g,
                                                    uint8_t b, uint8_t a) {
  std::vector<u8> data = {r, g, b, a};
  auto tex = std::make_shared<LX_core::Texture>(
      LX_core::TextureDesc{1, 1, LX_core::TextureFormat::RGBA8},
      std::move(data));
  return std::make_shared<LX_core::CombinedTextureSampler>(std::move(tex));
}

} // namespace

LX_core::CombinedTextureSamplerSharedPtr getPlaceholderWhite() {
  static auto tex = makePlaceholder(255, 255, 255, 255);
  return tex;
}

LX_core::CombinedTextureSamplerSharedPtr getPlaceholderBlack() {
  static auto tex = makePlaceholder(0, 0, 0, 255);
  return tex;
}

LX_core::CombinedTextureSamplerSharedPtr getPlaceholderNormal() {
  static auto tex = makePlaceholder(128, 128, 255, 255);
  return tex;
}

LX_core::CombinedTextureSamplerSharedPtr
resolvePlaceholder(const std::string &name) {
  if (name == "white")
    return getPlaceholderWhite();
  if (name == "black")
    return getPlaceholderBlack();
  if (name == "normal")
    return getPlaceholderNormal();
  return nullptr;
}

} // namespace LX_infra
