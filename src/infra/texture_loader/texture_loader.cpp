#define STB_IMAGE_IMPLEMENTATION
#include "texture_loader.hpp"
#include <stb/stb_image.h>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace infra {

struct TextureLoader::Impl {
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* data = nullptr;

  ~Impl() {
    if (data) {
      stbi_image_free(data);
    }
  }
};

TextureLoader::TextureLoader() : pImpl(std::make_unique<Impl>()) {}

TextureLoader::~TextureLoader() = default;

void TextureLoader::load(const std::string& filename) {
  int width, height, channels;
  stbi_set_flip_vertically_on_load(true);
  unsigned char* imageData = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);

  if (!imageData) {
    throw std::runtime_error("Failed to load texture: " + filename);
  }

  pImpl->width = width;
  pImpl->height = height;
  pImpl->channels = channels;
  pImpl->data = imageData;
}

int TextureLoader::getWidth() const {
  return pImpl->width;
}

int TextureLoader::getHeight() const {
  return pImpl->height;
}

int TextureLoader::getChannels() const {
  return pImpl->channels;
}

const unsigned char* TextureLoader::getData() const {
  return pImpl->data;
}

LX_core::TextureSharedPtr
TextureLoader::loadHdrTexture(const std::filesystem::path &filename) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_set_flip_vertically_on_load(false);
  std::unique_ptr<float, decltype(&stbi_image_free)> imageData(
      stbi_loadf(filename.string().c_str(), &width, &height, &channels,
                 STBI_rgb_alpha),
      stbi_image_free);
  if (!imageData) {
    throw std::runtime_error("Failed to load HDR texture: " +
                             filename.string());
  }

  LX_core::TextureDesc desc;
  desc.width = static_cast<u32>(width);
  desc.height = static_cast<u32>(height);
  desc.format = LX_core::TextureFormat::RGBA32Float;

  const usize byteCount = LX_core::expectedTextureByteCount(desc);
  std::vector<u8> pixels(byteCount);
  std::memcpy(pixels.data(), imageData.get(), byteCount);

  return std::make_shared<LX_core::Texture>(desc, std::move(pixels));
}

} // namespace infra
