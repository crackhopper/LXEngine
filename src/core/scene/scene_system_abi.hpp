#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <cstddef>

namespace LX_core {

constexpr u32 kSceneSystemDescriptorSet = 0;
constexpr u32 kSceneSystemCameraBinding = 0;
constexpr u32 kSceneSystemLightBinding = 1;
constexpr u32 kSceneSystemObjectBinding = 2;
constexpr u32 kSceneSystemMaterialBinding = 3;

struct alignas(16) SceneSystemCameraData final {
  Vec4f view;
  Vec4f projection;
  Vec4f eye;
};

struct alignas(16) SceneSystemLightData final {
  Vec4f directionIntensity;
  Vec4f colorEnvironment;
};

struct alignas(16) SceneSystemObjectData final {
  Vec4f objectToWorld0;
  Vec4f objectToWorld1;
  Vec4f objectToWorld2;
  Vec4f objectToWorld3;
  Vec4f worldToObject0;
  Vec4f worldToObject1;
  Vec4f worldToObject2;
  Vec4f worldToObject3;
};

struct alignas(16) SceneSystemMaterialInstanceData final {
  Vec4f baseColor;
  Vec4f bsdfParams0;
  Vec4f bsdfParams1;
  Vec4f textureIndices;
};

static_assert(sizeof(SceneSystemCameraData) == 48);
static_assert(sizeof(SceneSystemLightData) == 32);
static_assert(sizeof(SceneSystemObjectData) == 128);
static_assert(sizeof(SceneSystemMaterialInstanceData) == 64);
static_assert(offsetof(SceneSystemCameraData, view) == 0);
static_assert(offsetof(SceneSystemLightData, directionIntensity) == 0);
static_assert(offsetof(SceneSystemObjectData, objectToWorld0) == 0);
static_assert(offsetof(SceneSystemMaterialInstanceData, baseColor) == 0);

} // namespace LX_core
