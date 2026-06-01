#pragma once

#include "core/math/vec.hpp"
#include "core/offline/offline_scene.hpp"

#include <vector>

namespace LX_core::backend::offline {

/*
@source_analysis.section GpuSceneBuilder 固定 C++ 与 GLSL 的 buffer 合同
`GpuSceneBuilder` 把 `OfflineSceneIR` 打包成 compute shader 可以直接读取的
std430 storage buffer 数据。这里的结构体大小通过 `static_assert` 固定，
因为 `assets/shaders/glsl/offline_primary_ray.comp` 会按相同字段顺序解释这些
buffer。

这层也是未来 path tracing 扩展最容易出错的边界：新增材质参数、纹理索引、
light buffer 或 AOV 输出时，必须同步修改 C++ struct、GLSL struct、descriptor
layout 和 `test_offline_gpu_scene` 的 layout contract。
*/
struct alignas(16) GpuTriangle final {
  Vec4f v0{};
  Vec4f v1{};
  Vec4f v2{};
  Vec4f normal{};
  u32 materialIndex = 0;
  u32 objectIndex = 0;
  u32 pad0 = 0;
  u32 pad1 = 0;
};

struct alignas(16) GpuMaterial final {
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f params{0.0f, 0.5f, 0.0f, 0.0f};
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
};

struct alignas(16) GpuCameraParams final {
  Vec4f eye{};
  Vec4f cameraRight{};
  Vec4f cameraUp{};
  Vec4f cameraForward{};
  Vec4f lightDirectionIntensity{};
  Vec4f lightColorEnvironment{};
  u32 width = 0;
  u32 height = 0;
  u32 samples = 1;
  u32 seed = 1;
  u32 triangleCount = 0;
  u32 bvhNodeCount = 0;
  u32 materialCount = 0;
  u32 pad0 = 0;
};

struct GpuSceneData final {
  std::vector<GpuTriangle> triangles;
  std::vector<GpuMaterial> materials;
  GpuCameraParams params;
};

class GpuSceneBuilder final {
public:
  [[nodiscard]] GpuSceneData build(
      const LX_core::offline::OfflineSceneIR &scene,
      const LX_core::offline::OfflineRenderProfile &profile) const;
};

static_assert(sizeof(GpuTriangle) == 80);
static_assert(sizeof(GpuMaterial) == 48);
static_assert(sizeof(GpuCameraParams) == 128);

} // namespace LX_core::backend::offline
