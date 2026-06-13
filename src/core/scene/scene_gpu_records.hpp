#pragma once

#include "core/math/bounds.hpp"
#include "core/math/mat.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <array>
#include <cstddef>

namespace LX_core {

class MaterialInstance;

constexpr u32 kSceneGpuAttributeSemanticNormal0 = 1;
constexpr u32 kSceneGpuAttributeSemanticUv0 = 2;
constexpr u32 kSceneGpuAttributeSemanticTangent0 = 3;
constexpr u32 kSceneGpuAttributeSemanticColor0 = 4;
constexpr u32 kSceneGpuAttributeSemanticSkinWeights0 = 5;

struct alignas(16) SceneGpuAttributeStreamRecord final {
  u32 semantic = 0;
  u32 valueOffset = 0;
  u32 valueCount = 0;
  u32 components = 0;
};

struct alignas(16) SceneGpuMeshRecord final {
  u32 vertexOffset = 0;
  u32 indexOffset = 0;
  u32 indexCount = 0;
  u32 geometryIndex = 0;
  u32 attributeStreamOffset = 0;
  u32 attributeStreamCount = 0;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};

struct alignas(16) SceneGpuPrimitiveRecord final {
  u32 indexOffset = 0;
  u32 meshIndex = 0;
  u32 materialIndex = 0;
  u32 objectIndex = 0;
};

struct alignas(16) SceneGpuMaterialRefRecord final {
  u32 sourceStorageIndex = 0xffffffffu;
  u32 sourceLocalMaterialIndex = 0xffffffffu;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};

struct alignas(16) SceneGpuDrawRecord final {
  u32 objectIndex = 0;
  u32 materialIndex = 0;
  u32 meshIndex = 0;
  u32 materialRefIndex = 0xffffffffu;
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
  Vec4f clearcoatParams{0.0f, 0.04f, 0.0f, 0.0f};
  u32 baseColorTexture = 0xffffffffu;
  u32 normalTexture = 0xffffffffu;
  u32 metallicRoughnessTexture = 0xffffffffu;
  u32 aoTexture = 0xffffffffu;
  u32 emissiveTexture = 0xffffffffu;
  u32 flags = 0;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};

struct alignas(16) SceneGpuRenderPathGraphRecord final {
  u32 passOffset = 0;
  u32 passCount = 0;
  u32 featureOffset = 0;
  u32 featureCount = 0;
};

struct alignas(16) SceneGpuRenderPathGraphPassRecord final {
  u32 shaderIndex = 0xffffffffu;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
  u32 reserved2 = 0;
};

struct alignas(16) SceneGpuRenderPathGraphFeatureRecord final {
  u32 featureIndex = 0xffffffffu;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
  u32 reserved2 = 0;
};

constexpr u32 kSceneGpuMaterialCullModeMask = 0x3u;
constexpr u32 kSceneGpuMaterialCullModeNone = 0u;
constexpr u32 kSceneGpuMaterialCullModeFront = 1u;
constexpr u32 kSceneGpuMaterialCullModeBack = 2u;

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

static_assert(sizeof(SceneGpuAttributeStreamRecord) == 16);
static_assert(sizeof(SceneGpuMeshRecord) == 32);
static_assert(sizeof(SceneGpuPrimitiveRecord) == 16);
static_assert(sizeof(SceneGpuMaterialRefRecord) == 16);
static_assert(sizeof(SceneGpuDrawRecord) == 16);
static_assert(sizeof(SceneGpuObjectRecord) == 176);
static_assert(sizeof(SceneGpuMaterialRecord) == 96);
static_assert(sizeof(SceneGpuRenderPathGraphRecord) == 16);
static_assert(sizeof(SceneGpuRenderPathGraphPassRecord) == 16);
static_assert(sizeof(SceneGpuRenderPathGraphFeatureRecord) == 16);
static_assert(sizeof(SceneGpuFrameParams) == 176);
static_assert(offsetof(SceneGpuObjectRecord, objectToWorld) == 0);
static_assert(offsetof(SceneGpuObjectRecord, worldToObject) == 64);
static_assert(offsetof(SceneGpuObjectRecord, boundsMin) == 128);
static_assert(offsetof(SceneGpuObjectRecord, boundsMax) == 144);
static_assert(offsetof(SceneGpuObjectRecord, visible) == 160);
static_assert(offsetof(SceneGpuObjectRecord, flags) == 164);
static_assert(offsetof(SceneGpuObjectRecord, visibilityMask) == 168);
static_assert(offsetof(SceneGpuObjectRecord, debugId) == 172);
static_assert(offsetof(SceneGpuDrawRecord, objectIndex) == 0);
static_assert(offsetof(SceneGpuDrawRecord, materialIndex) == 4);
static_assert(offsetof(SceneGpuDrawRecord, meshIndex) == 8);
static_assert(offsetof(SceneGpuDrawRecord, materialRefIndex) == 12);
static_assert(offsetof(SceneGpuMaterialRefRecord, sourceStorageIndex) == 0);
static_assert(offsetof(SceneGpuMaterialRefRecord, sourceLocalMaterialIndex) ==
              4);
static_assert(offsetof(SceneGpuMaterialRecord, baseColor) == 0);
static_assert(offsetof(SceneGpuMaterialRecord, pbrParams) == 16);
static_assert(offsetof(SceneGpuMaterialRecord, emissive) == 32);
static_assert(offsetof(SceneGpuMaterialRecord, clearcoatParams) == 48);
static_assert(offsetof(SceneGpuMaterialRecord, baseColorTexture) == 64);
static_assert(offsetof(SceneGpuMaterialRecord, normalTexture) == 68);
static_assert(offsetof(SceneGpuMaterialRecord, metallicRoughnessTexture) == 72);
static_assert(offsetof(SceneGpuMaterialRecord, aoTexture) == 76);
static_assert(offsetof(SceneGpuMaterialRecord, emissiveTexture) == 80);
static_assert(offsetof(SceneGpuMaterialRecord, flags) == 84);
static_assert(offsetof(SceneGpuMaterialRecord, reserved0) == 88);
static_assert(offsetof(SceneGpuMaterialRecord, reserved1) == 92);

} // namespace LX_core
