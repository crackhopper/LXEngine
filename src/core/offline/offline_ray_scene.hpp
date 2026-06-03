#pragma once

#include "core/math/vec.hpp"
#include "core/offline/offline_scene.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <array>
#include <vector>

namespace LX_core::offline {

struct alignas(16) OfflineVertexRecord final {
  Vec4f position{};
  Vec4f normal{};
  Vec4f uvTangentSign{};
  Vec4f tangent{};
};

struct alignas(16) OfflineMeshRecord final {
  u32 vertexOffset = 0;
  u32 indexOffset = 0;
  u32 indexCount = 0;
  u32 geometryIndex = 0;
};

struct alignas(16) OfflinePrimitiveRecord final {
  u32 indexOffset = 0;
  u32 meshIndex = 0;
  u32 materialIndex = 0;
  u32 objectIndex = 0;
};

struct alignas(16) OfflineObjectRecord final {
  std::array<Vec4f, 4> objectToWorld{};
  std::array<Vec4f, 4> worldToObject{};
  Vec4f boundsMin{};
  Vec4f boundsMax{};
  u32 visible = 1;
  u32 flags = 0;
  u32 pad0 = 0;
  u32 pad1 = 0;
};

struct alignas(16) OfflineMaterialRecord final {
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f params{0.0f, 0.5f, 0.0f, 0.0f};
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
};

struct alignas(16) OfflineSceneParams final {
  Vec4f eye{};
  Vec4f cameraRight{};
  Vec4f cameraUp{};
  Vec4f cameraForward{};
  Vec4f lightDirectionIntensity{};
  Vec4f lightColorEnvironment{};
  Vec4f backgroundColor{};
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
  u32 pad1 = 0;
  u32 pad2 = 0;
};

struct alignas(16) OfflineBvhNode final {
  Vec4f boundsMinLeftFirst{};
  Vec4f boundsMaxCount{};
};

struct OfflineRayScene final {
  SceneResourceTable resourceTable;
  RenderSceneSnapshot snapshot;
  std::vector<OfflineVertexRecord> vertices;
  std::vector<u32> indices;
  std::vector<OfflineMeshRecord> meshes;
  std::vector<OfflinePrimitiveRecord> primitives;
  std::vector<OfflineObjectRecord> objects;
  std::vector<OfflineMaterialRecord> materials;
  std::vector<OfflineBvhNode> bvhNodes;
  OfflineSceneParams params;
};

class OfflineBvhBuilder final {
public:
  void build(OfflineRayScene &scene) const;
};

class OfflineRaySceneBuilder final {
public:
  [[nodiscard]] OfflineRayScene build(const OfflineSceneIR &scene,
                                      const OutputProfile &output,
                                      const OfflineRenderSettings &offline) const;
};

static_assert(sizeof(OfflineVertexRecord) == 64);
static_assert(sizeof(OfflineMeshRecord) == 16);
static_assert(sizeof(OfflinePrimitiveRecord) == 16);
static_assert(sizeof(OfflineObjectRecord) == 176);
static_assert(sizeof(OfflineMaterialRecord) == 48);
static_assert(sizeof(OfflineSceneParams) == 160);
static_assert(sizeof(OfflineBvhNode) == 32);

} // namespace LX_core::offline
