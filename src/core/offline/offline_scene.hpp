#pragma once

#include "core/math/mat.hpp"
#include "core/math/vec.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/platform/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_core::offline {

/*
@source_analysis.section OfflineSceneIR 是离线实验室的标准样品
`OfflineSceneIR` 是实时 scene 文档和离线 integrator 之间的隔离层。
实时渲染需要 `SceneNode`、component、FrameGraph、material pass 和 editor
状态；离线渲染只需要相机、几何、材质、光源、环境以及可复现实验参数。

因此这组 IR 类型刻意不携带 Vulkan 句柄，也不直接复用实时
`RenderingItem`。它把 `.scene.yaml` 中能够离线计算的事实收敛成稳定数据，
让 CPU compiler、GPU packing、path tracing shader 和输出模块可以独立演进。
*/
struct OfflineCameraIR final {
  std::string path;
  Vec3f eye{0.0f, 1.7f, 5.0f};
  Vec3f target{0.0f, 0.8f, 0.0f};
  Vec3f up{0.0f, 1.0f, 0.0f};
  float fovYDegrees = 45.0f;
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
};

struct OfflineMaterialIR final {
  std::string name;
  std::string materialModel = "pbr-metallic-roughness";
  Vec3f baseColor{0.8f, 0.8f, 0.8f};
  std::optional<std::string> albedoTextureRef;
  float metallic = 0.0f;
  float roughness = 0.5f;
  Vec3f emissive{0.0f, 0.0f, 0.0f};
};

struct OfflineVertexIR final {
  Vec3f position{};
  Vec3f normal{0.0f, 1.0f, 0.0f};
  Vec2f uv{};
};

struct OfflineMeshIR final {
  std::string name;
  std::string sourceUri;
  std::vector<OfflineVertexIR> vertices;
  std::vector<u32> indices;
};

struct OfflineInstanceIR final {
  std::string path;
  u32 meshIndex = 0;
  u32 materialIndex = 0;
  Mat4f worldTransform = Mat4f::identity();
  bool visible = true;
};

struct OfflineDirectionalLightIR final {
  std::string path;
  Vec3f direction{-0.35f, -1.0f, -0.25f};
  Vec3f color{1.0f, 0.96f, 0.88f};
  float intensity = 1.0f;
};

struct OfflineEnvironmentIR final {
  bool enabled = false;
  std::string hdrUri;
  float intensity = 1.0f;
};

struct OfflineSceneIR final {
  std::string name;
  std::string cameraPath;
  OfflineCameraIR camera;
  OfflineEnvironmentIR environment;
  std::vector<OfflineMaterialIR> materials;
  std::vector<OfflineMeshIR> meshes;
  std::vector<OfflineInstanceIR> instances;
  std::vector<OfflineDirectionalLightIR> directionalLights;
  std::vector<std::string> warnings;
};

struct OfflineRenderJob final {
  OfflineSceneIR scene;
  OfflineRenderProfile profile;
  std::filesystem::path outputPath;
  std::string cameraPath;
};

struct OfflineReadbackImage final {
  u32 width = 0;
  u32 height = 0;
  std::vector<float> rgba;

  [[nodiscard]] usize pixelCount() const {
    return static_cast<usize>(width) * static_cast<usize>(height);
  }
};

[[nodiscard]] Vec3f transformPoint(const Mat4f &matrix, const Vec3f &point);
[[nodiscard]] Vec3f transformVector(const Mat4f &matrix, const Vec3f &vector);

} // namespace LX_core::offline
