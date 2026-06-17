module;
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>

export module LX_New_Common.Math:Ray;

import LX_New_Common.Platform;
import :Vec;
import :Shape;

export namespace LX_New_Common {

struct Ray {
  Vec3f origin;
  Vec3f direction;
};

inline std::optional<f32> intersectRayBox(const Ray &ray,
                                          const BoundingBox &box) {
  if (!box.isValid()) {
    return std::nullopt;
  }

  constexpr f32 epsilon = 1e-8f;
  f32 tMin = 0.0f;
  f32 tMax = std::numeric_limits<f32>::infinity();

  const auto updateAxis = [&](f32 originVal, f32 directionVal, f32 minValue,
                              f32 maxValue) -> bool {
    if (std::abs(directionVal) <= epsilon) {
      return originVal >= minValue && originVal <= maxValue;
    }

    const f32 invDirection = 1.0f / directionVal;
    f32 axisMin = (minValue - originVal) * invDirection;
    f32 axisMax = (maxValue - originVal) * invDirection;
    if (axisMin > axisMax) {
      std::swap(axisMin, axisMax);
    }

    tMin = std::max(tMin, axisMin);
    tMax = std::min(tMax, axisMax);
    return tMin <= tMax;
  };

  if (!updateAxis(ray.origin.x, ray.direction.x, box.min.x, box.max.x) ||
      !updateAxis(ray.origin.y, ray.direction.y, box.min.y, box.max.y) ||
      !updateAxis(ray.origin.z, ray.direction.z, box.min.z, box.max.z)) {
    return std::nullopt;
  }

  if (tMax <= epsilon) {
    return std::nullopt;
  }

  return tMin > epsilon ? std::optional<f32>(tMin) : std::optional<f32>(tMax);
}

} // namespace LX_New_Common
