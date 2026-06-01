#include "core/offline/offline_scene.hpp"

namespace LX_core::offline {

Vec3f transformPoint(const Mat4f &matrix, const Vec3f &point) {
  const Vec4f result = matrix * Vec4f{point.x, point.y, point.z, 1.0f};
  if (result.w == 0.0f) {
    return Vec3f{result.x, result.y, result.z};
  }
  return Vec3f{result.x / result.w, result.y / result.w, result.z / result.w};
}

Vec3f transformVector(const Mat4f &matrix, const Vec3f &vector) {
  const Vec4f result = matrix * Vec4f{vector.x, vector.y, vector.z, 0.0f};
  return Vec3f{result.x, result.y, result.z};
}

} // namespace LX_core::offline
