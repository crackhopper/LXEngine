#pragma once
#include "core/platform/types.hpp"
#include "core/math/vec.hpp"
#include <memory>
#include <string>
#include <vector>

namespace infra {

class ObjLoader {
public:
  ObjLoader();
  ~ObjLoader();

  void load(const std::string &filename);

  const std::vector<LX_core::Vec3f> &getPositions() const;
  const std::vector<LX_core::Vec3f> &getNormals() const;
  const std::vector<LX_core::Vec2f> &getTexCoords() const;
  const std::vector<u32> &getIndices() const;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace infra
