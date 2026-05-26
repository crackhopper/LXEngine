#pragma once
#include "core/asset/texture.hpp"
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace infra {

class TextureLoader {
public:
  TextureLoader();
  ~TextureLoader();

  void load(const std::string& filename);

  int getWidth() const;
  int getHeight() const;
  int getChannels() const;
  const unsigned char* getData() const;

  static LX_core::TextureSharedPtr loadHdrTexture(
      const std::filesystem::path &filename);

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace infra
