#pragma once

#include "core/platform/types.hpp"

namespace LX_core {

struct ResourceHandleBase {
  u32 index = 0;
  u32 generation = 0;

  [[nodiscard]] bool isValid() const { return generation != 0; }
  bool operator==(const ResourceHandleBase &other) const = default;
};

struct GeometryStorageHandle final : ResourceHandleBase {};
struct MeshHandle final : ResourceHandleBase {};
struct MaterialHandle final : ResourceHandleBase {};
struct TextureHandle final : ResourceHandleBase {};
struct LightHandle final : ResourceHandleBase {};
struct SkeletonHandle final : ResourceHandleBase {};
struct CameraHandle final : ResourceHandleBase {};
struct ObjectHandle final : ResourceHandleBase {};

} // namespace LX_core
