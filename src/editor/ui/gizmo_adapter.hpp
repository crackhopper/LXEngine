#pragma once

#include "core/math/mat.hpp"
#include "core/math/vec.hpp"

namespace LX_core {

struct GizmoTransformComponents final {
  Vec3f translation{0.0f, 0.0f, 0.0f};
  Vec3f rotationEulerDegrees{0.0f, 0.0f, 0.0f};
  Vec3f scale{1.0f, 1.0f, 1.0f};
};

class GizmoAdapter final {
public:
  static void toFloat16(const Mat4f &matrix, float out[16]);
  [[nodiscard]] static Mat4f fromFloat16(const float in[16]);
  [[nodiscard]] static GizmoTransformComponents decompose(const Mat4f &matrix);
  [[nodiscard]] static Mat4f compose(const GizmoTransformComponents &components);
};

} // namespace LX_core
