#include "placeholder_textures.hpp"

namespace LX_infra {

namespace {

LX_core::CombinedTextureSamplerSharedPtr
makePlaceholder(u8 r, u8 g, u8 b, u8 a, LX_core::TextureContent content) {
  std::vector<u8> data = {r, g, b, a};
  auto tex = std::make_shared<LX_core::Texture>(
      LX_core::TextureDesc{1, 1, LX_core::TextureFormat::RGBA8, content},
      std::move(data));
  return std::make_shared<LX_core::CombinedTextureSampler>(std::move(tex));
}

} // namespace

LX_core::CombinedTextureSamplerSharedPtr getPlaceholderWhite() {
  static auto tex =
      makePlaceholder(255, 255, 255, 255, LX_core::TextureContent::Color);
  return tex;
}

LX_core::CombinedTextureSamplerSharedPtr getPlaceholderBlack() {
  static auto tex =
      makePlaceholder(0, 0, 0, 255, LX_core::TextureContent::Color);
  return tex;
}

LX_core::CombinedTextureSamplerSharedPtr getPlaceholderNormal() {
  static auto tex =
      makePlaceholder(128, 128, 255, 255, LX_core::TextureContent::Normal);
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
