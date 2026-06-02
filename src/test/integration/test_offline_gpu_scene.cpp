#include "core/offline/offline_ray_scene.hpp"
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

void testRaySceneUsesSharedIndexedResourcesAndBuildsBvh() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "ibl_metal_sphere.scene.yaml";
  LX_infra::offline::OfflineSceneCompiler compiler{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto scene = compiler.compileFile(scenePath, "/game_cam");

  LX_core::offline::OutputProfile output;
  output.width = 64;
  output.height = 64;
  LX_core::offline::OfflineRenderSettings offline;
  offline.samples = 1;
  LX_core::offline::OfflineRaySceneBuilder sceneBuilder;
  auto rayScene = sceneBuilder.build(scene, output, offline);

  EXPECT(!rayScene.vertices.empty(), "ray scene should contain shared vertices");
  EXPECT(!rayScene.indices.empty(), "ray scene should contain shared indices");
  EXPECT(!rayScene.primitives.empty(), "ray scene should contain primitives");
  EXPECT(rayScene.params.primitiveCount == rayScene.primitives.size(),
         "primitive count should match packed buffer");
  EXPECT(rayScene.snapshot.meshHandles.size() == scene.meshes.size(),
         "snapshot should expose shared mesh handles");
  EXPECT(rayScene.vertices.size() < rayScene.primitives.size() * 3,
         "indexed ray scene should not duplicate three vertices per primitive");

  bool foundElevatedSphereTriangle = false;
  for (const auto &primitive : rayScene.primitives) {
    const auto &object = rayScene.objects.at(primitive.objectIndex);
    if (primitive.objectIndex == 1 && object.objectToWorld[3].y > 0.1f) {
      foundElevatedSphereTriangle = true;
      break;
    }
  }
  EXPECT(foundElevatedSphereTriangle,
         "sphere instance transform should move primitives above the plane");
  EXPECT(!rayScene.bvhNodes.empty(), "BVH should contain nodes");
  EXPECT(rayScene.params.bvhNodeCount == rayScene.bvhNodes.size(),
         "BVH node count should match params");
}

void testRayLayoutContract() {
  EXPECT(sizeof(LX_core::offline::OfflineVertexRecord) == 64,
         "OfflineVertexRecord std430 contract should stay stable");
  EXPECT(sizeof(LX_core::offline::OfflineMeshRecord) == 16,
         "OfflineMeshRecord std430 contract should stay stable");
  EXPECT(sizeof(LX_core::offline::OfflinePrimitiveRecord) == 16,
         "OfflinePrimitiveRecord std430 contract should stay stable");
  EXPECT(sizeof(LX_core::offline::OfflineObjectRecord) == 176,
         "OfflineObjectRecord std430 contract should stay stable");
  EXPECT(sizeof(LX_core::offline::OfflineMaterialRecord) == 48,
         "OfflineMaterialRecord std430 contract should stay stable");
  EXPECT(sizeof(LX_core::offline::OfflineBvhNode) == 32,
         "OfflineBvhNode std430 contract should stay stable");
  EXPECT(sizeof(LX_core::offline::OfflineSceneParams) == 144,
         "OfflineSceneParams std430 contract should stay stable");
}

void testIndexedVertexNormalsArePreserved() {
  LX_core::offline::OfflineSceneIR scene;
  scene.name = "normal interpolation";
  scene.materials.push_back({});
  scene.meshes.push_back(LX_core::offline::OfflineMeshIR{
      .name = "triangle",
      .vertices =
          {
              {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
              {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
              {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
          },
      .indices = {0, 1, 2},
  });
  scene.instances.push_back(LX_core::offline::OfflineInstanceIR{});

  LX_core::offline::OutputProfile output;
  output.width = 16;
  output.height = 16;
  LX_core::offline::OfflineRaySceneBuilder builder;
  const auto rayScene =
      builder.build(scene, output, LX_core::offline::OfflineRenderSettings{});

  EXPECT(rayScene.vertices.size() == 3,
         "single triangle should keep three shared vertex records");
  EXPECT(rayScene.primitives.size() == 1,
         "single triangle should produce one primitive record");
  EXPECT(rayScene.vertices[0].normal.x == 1.0f &&
             rayScene.vertices[1].normal.y == 1.0f &&
             rayScene.vertices[2].normal.z == 1.0f,
         "per-vertex normals should be preserved for barycentric interpolation");
}

} // namespace

int main() {
  testRayLayoutContract();
  testIndexedVertexNormalsArePreserved();
  testRaySceneUsesSharedIndexedResourcesAndBuildsBvh();
  if (failures != 0) {
    std::cerr << "test_offline_gpu_scene failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_gpu_scene passed\n";
  return 0;
}
