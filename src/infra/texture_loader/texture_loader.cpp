#define STB_IMAGE_IMPLEMENTATION
#include "texture_loader.hpp"
#include <stb/stb_image.h>
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

TextureLoader::TextureLoader() : pImpl(new Impl) {}

TextureLoader::~TextureLoader() {
  delete pImpl;
}

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

} // namespace infra
