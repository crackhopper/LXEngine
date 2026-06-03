#include "core/scene/scene_gpu_records.hpp"

namespace LX_core {

std::array<Vec4f, 4> toGpuRows(const Mat4f &matrix) {
  return {{
      {matrix(0, 0), matrix(0, 1), matrix(0, 2), matrix(0, 3)},
      {matrix(1, 0), matrix(1, 1), matrix(1, 2), matrix(1, 3)},
      {matrix(2, 0), matrix(2, 1), matrix(2, 2), matrix(2, 3)},
      {matrix(3, 0), matrix(3, 1), matrix(3, 2), matrix(3, 3)},
  }};
}

Vec4f toGpuBoundsMin(const BoundingBox &bounds) {
  if (!bounds.isValid()) {
    return {};
  }
  return {bounds.min.x, bounds.min.y, bounds.min.z, 0.0f};
}

Vec4f toGpuBoundsMax(const BoundingBox &bounds) {
  if (!bounds.isValid()) {
    return {};
  }
  return {bounds.max.x, bounds.max.y, bounds.max.z, 0.0f};
}

} // namespace LX_core
