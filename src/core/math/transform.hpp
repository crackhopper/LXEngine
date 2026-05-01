#pragma once

#include "core/math/mat.hpp"
#include "core/math/quat.hpp"
#include "core/math/vec.hpp"

namespace LX_core {

struct Transform {
  Vec3f translation{0.0f, 0.0f, 0.0f};
  Quatf rotation{};
  Vec3f scale{1.0f, 1.0f, 1.0f};

  [[nodiscard]] static Transform identity() { return Transform{}; }

  [[nodiscard]] Mat4f toMat4() const;
  [[nodiscard]] static Transform fromMat4(const Mat4f &m);

  [[nodiscard]] Transform normalized() const {
    Transform copy = *this;
    copy.rotation = copy.rotation.normalized();
    return copy;
  }

  [[nodiscard]] Transform operator*(const Transform &other) const;
};

} // namespace LX_core
