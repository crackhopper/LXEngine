#pragma once

#include "core/math/bounds.hpp"
#include "core/math/mat.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <array>
#include <cstddef>

namespace LX_core {

class MaterialInstance;

struct alignas(16) SceneGpuVertexRecord final {
  Vec4f position{};
  Vec4f normal{};
  Vec4f uvTangentSign{};
  Vec4f tangent{};
};

struct alignas(16) SceneGpuMeshRecord final {
  u32 vertexOffset = 0;
  u32 indexOffset = 0;
  u32 indexCount = 0;
  u32 geometryIndex = 0;
};

struct alignas(16) SceneGpuPrimitiveRecord final {
  u32 indexOffset = 0;
  u32 meshIndex = 0;
  u32 materialIndex = 0;
  u32 objectIndex = 0;
};

struct alignas(16) SceneGpuObjectRecord final {
  std::array<Vec4f, 4> objectToWorld{};
  std::array<Vec4f, 4> worldToObject{};
  Vec4f boundsMin{};
  Vec4f boundsMax{};
  u32 visible = 1;
  u32 flags = 0;
  u32 visibilityMask = 0xffffffffu;
  u32 debugId = 0;
};

struct alignas(16) SceneGpuMaterialRecord final {
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f pbrParams{0.0f, 0.5f, 0.0f, 0.0f};
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
  u32 baseColorTexture = 0xffffffffu;
  u32 normalTexture = 0xffffffffu;
  u32 metallicRoughnessTexture = 0xffffffffu;
  u32 aoTexture = 0xffffffffu;
  u32 emissiveTexture = 0xffffffffu;
  u32 flags = 0;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};

struct alignas(16) SceneGpuFrameParams final {
  Vec4f eye{};
  Vec4f cameraRight{};
  Vec4f cameraUp{};
  Vec4f cameraForward{};
  Vec4f lightDirectionIntensity{};
  Vec4f lightColorEnvironment{};
  Vec4f backgroundColor{};
  Vec4f reserved{};
  u32 width = 0;
  u32 height = 0;
  u32 samples = 1;
  u32 seed = 1;
  u32 primitiveCount = 0;
  u32 bvhNodeCount = 0;
  u32 materialCount = 0;
  u32 maxBounce = 1;
  u32 shadowsEnabled = 1;
  u32 compareMode = 0;
  u32 integrator = 0;
  u32 frameIndex = 0;
};

[[nodiscard]] std::array<Vec4f, 4> toGpuColumns(const Mat4f &matrix);
[[nodiscard]] Vec4f toGpuBoundsMin(const BoundingBox &bounds);
[[nodiscard]] Vec4f toGpuBoundsMax(const BoundingBox &bounds);
[[nodiscard]] SceneGpuMaterialRecord
toGpuMaterialRecord(const MaterialInstance &material);

static_assert(sizeof(SceneGpuVertexRecord) == 64);
static_assert(sizeof(SceneGpuMeshRecord) == 16);
static_assert(sizeof(SceneGpuPrimitiveRecord) == 16);
static_assert(sizeof(SceneGpuObjectRecord) == 176);
static_assert(sizeof(SceneGpuMaterialRecord) == 80);
static_assert(sizeof(SceneGpuFrameParams) == 176);
static_assert(offsetof(SceneGpuObjectRecord, objectToWorld) == 0);
static_assert(offsetof(SceneGpuObjectRecord, worldToObject) == 64);
static_assert(offsetof(SceneGpuObjectRecord, boundsMin) == 128);
static_assert(offsetof(SceneGpuObjectRecord, boundsMax) == 144);
static_assert(offsetof(SceneGpuObjectRecord, visible) == 160);
static_assert(offsetof(SceneGpuObjectRecord, flags) == 164);
static_assert(offsetof(SceneGpuObjectRecord, visibilityMask) == 168);
static_assert(offsetof(SceneGpuObjectRecord, debugId) == 172);
static_assert(offsetof(SceneGpuMaterialRecord, baseColor) == 0);
static_assert(offsetof(SceneGpuMaterialRecord, pbrParams) == 16);
static_assert(offsetof(SceneGpuMaterialRecord, emissive) == 32);
static_assert(offsetof(SceneGpuMaterialRecord, baseColorTexture) == 48);
static_assert(offsetof(SceneGpuMaterialRecord, normalTexture) == 52);
static_assert(offsetof(SceneGpuMaterialRecord, metallicRoughnessTexture) == 56);
static_assert(offsetof(SceneGpuMaterialRecord, aoTexture) == 60);
static_assert(offsetof(SceneGpuMaterialRecord, emissiveTexture) == 64);
static_assert(offsetof(SceneGpuMaterialRecord, flags) == 68);
static_assert(offsetof(SceneGpuMaterialRecord, reserved0) == 72);
static_assert(offsetof(SceneGpuMaterialRecord, reserved1) == 76);

} // namespace LX_core
