#pragma once

#include "core/math/bounds.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>

namespace LX_core {

struct Ray {
  Vec3f origin;
  Vec3f direction;
};

inline std::optional<float> intersectRayBox(const Ray &ray,
                                            const BoundingBox &box) {
  assert(ray.direction.length2() > 1e-16f &&
         "intersectRayBox requires non-zero ray direction");
  if (!box.isValid()) {
    return std::nullopt;
  }

  constexpr float epsilon = 1e-8f;
  float tMin = 0.0f;
  float tMax = std::numeric_limits<float>::infinity();

  const auto updateAxis = [&](float origin, float direction, float minValue,
                              float maxValue) -> bool {
    if (std::abs(direction) <= epsilon) {
      return origin >= minValue && origin <= maxValue;
    }

    const float invDirection = 1.0f / direction;
    float axisMin = (minValue - origin) * invDirection;
    float axisMax = (maxValue - origin) * invDirection;
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

  if (tMax < 0.0f) {
    return std::nullopt;
  }

  return tMin > epsilon ? std::optional<float>(tMin) : std::nullopt;
}

} // namespace LX_core
