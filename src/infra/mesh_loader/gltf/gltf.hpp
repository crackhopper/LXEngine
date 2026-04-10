#pragma once
#include "core/math/vec.hpp"
#include <string>
#include <vector>

namespace infra {

class GLTFLoader {
public:
  GLTFLoader();
  ~GLTFLoader();

  void load(const std::string &filename);

  const std::vector<LX_core::Vec3f> &getPositions() const;
  const std::vector<LX_core::Vec3f> &getNormals() const;
  const std::vector<LX_core::Vec2f> &getTexCoords() const;
  const std::vector<uint32_t> &getIndices() const;

private:
  struct Impl;
  Impl *pImpl;
};

} // namespace infra
