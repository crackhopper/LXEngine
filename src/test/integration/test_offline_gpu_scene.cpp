#include "backend/vulkan/offline/compute_bvh_builder.hpp"
#include "backend/vulkan/offline/gpu_scene_builder.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_compiler.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
#include <iostream>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testGpuSceneAppliesInstanceTransformsAndBuildsBvh() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "ibl_metal_sphere.scene.yaml";
  LX_infra::offline::OfflineSceneCompiler compiler{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto scene = compiler.compileFile(scenePath, "/game_cam");

  LX_core::offline::OfflineRenderProfile profile;
  profile.width = 64;
  profile.height = 64;
  profile.samples = 1;
  LX_core::backend::offline::GpuSceneBuilder sceneBuilder;
  auto gpuScene = sceneBuilder.build(scene, profile);

  EXPECT(!gpuScene.triangles.empty(), "GPU scene should contain triangles");
  EXPECT(gpuScene.params.triangleCount == gpuScene.triangles.size(),
         "triangle count should match packed buffer");

  bool foundElevatedSphereTriangle = false;
  for (const auto &triangle : gpuScene.triangles) {
    if (triangle.objectIndex == 1 && triangle.v0.y > 0.1f) {
      foundElevatedSphereTriangle = true;
      break;
    }
  }
  EXPECT(foundElevatedSphereTriangle,
         "sphere instance transform should move triangles above the plane");

  LX_core::backend::offline::ComputeBvhBuilder bvhBuilder;
  const auto bvh = bvhBuilder.build(std::move(gpuScene.triangles));
  EXPECT(!bvh.nodes.empty(), "BVH should contain nodes");
  EXPECT(bvh.triangles.size() == gpuScene.params.triangleCount,
         "BVH should preserve triangle count");
}

void testGpuLayoutContract() {
  EXPECT(sizeof(LX_core::backend::offline::GpuTriangle) == 80,
         "GpuTriangle std430 contract should stay stable");
  EXPECT(sizeof(LX_core::backend::offline::GpuMaterial) == 48,
         "GpuMaterial std430 contract should stay stable");
  EXPECT(sizeof(LX_core::backend::offline::GpuBvhNode) == 32,
         "GpuBvhNode std430 contract should stay stable");
  EXPECT(sizeof(LX_core::backend::offline::GpuCameraParams) == 128,
         "GpuCameraParams std430 contract should stay stable");
}

} // namespace

int main() {
  testGpuLayoutContract();
  testGpuSceneAppliesInstanceTransformsAndBuildsBvh();
  if (failures != 0) {
    std::cerr << "test_offline_gpu_scene failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_gpu_scene passed\n";
  return 0;
}
