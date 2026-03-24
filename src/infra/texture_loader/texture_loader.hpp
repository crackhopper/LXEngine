#pragma once
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

private:
  struct Impl;
  Impl* pImpl;
};

} // namespace infra