#include "core/asset/builtin_meshes.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <algorithm>
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

void testIblMetalSphereLoadsToSceneResourceTable() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "ibl_metal_sphere.scene.yaml";
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto loaded = loader.loadFile(scenePath, "/game_cam");
  const auto &table = loaded.table;

  EXPECT(table.meshCount() >= 1, "scene table should contain meshes");
  EXPECT(table.materialCount() >= 1, "scene table should contain materials");
  EXPECT(table.objectCount() >= 1, "scene table should contain objects");
  EXPECT(table.cameraCount() == 1, "requested camera should load");
  EXPECT(!table.buildUploadView().primitives.empty(),
         "scene table should produce upload primitives");
}

void testBuiltinSphereUsesSharedPrimitiveMesh() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "realtime_offline_compare_diagnostic.scene.yaml";
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto loaded = loader.loadFile(scenePath, "/game_cam");
  const auto uploadView = loaded.table.buildUploadView();

  EXPECT(!uploadView.meshes.empty(), "builtin sphere mesh should load");
  EXPECT(!uploadView.vertices.empty(),
         "offline loader should expose shared builtin mesh vertices");
  EXPECT(!uploadView.indices.empty(),
         "offline loader should expose shared builtin mesh indices");
}

void testBuiltinSphereWindingMatchesOutwardNormals() {
  const auto sphere =
      LX_core::buildBuiltinPrimitiveMesh("builtin://lxe_editor/primitives/sphere");
  const auto *vertices =
      dynamic_cast<const LX_core::VertexBuffer<LX_core::VertexPosNormalUvBone> *>(
          sphere->getVertexBuffer().get());
  EXPECT(vertices != nullptr, "builtin sphere should expose typed vertices");
  if (vertices == nullptr) {
    return;
  }
  const auto *indices =
      static_cast<const u32 *>(sphere->getIndexBuffer()->getRawData());
  const auto *vertexData =
      static_cast<const LX_core::VertexPosNormalUvBone *>(vertices->getRawData());

  bool checkedTriangle = false;
  for (u32 i = 0; i + 2 < sphere->getIndexCount(); i += 3) {
    const auto &a = vertexData[indices[i + 0]];
    const auto &b = vertexData[indices[i + 1]];
    const auto &c = vertexData[indices[i + 2]];
    const LX_core::Vec3f faceNormal = (b.pos - a.pos).cross(c.pos - a.pos);
    if (faceNormal.length2() <= 1.0e-8f) {
      continue;
    }
    const LX_core::Vec3f averageNormal =
        (a.normal + b.normal + c.normal).normalized();
    EXPECT(faceNormal.dot(averageNormal) > 0.0f,
           "builtin sphere triangle winding should match outward normals");
    checkedTriangle = true;
  }
  EXPECT(checkedTriangle, "builtin sphere winding test should inspect triangles");
}

} // namespace

int main() {
  testIblMetalSphereLoadsToSceneResourceTable();
  testBuiltinSphereUsesSharedPrimitiveMesh();
  testBuiltinSphereWindingMatchesOutwardNormals();
  if (failures != 0) {
    std::cerr << "test_offline_scene_loader failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_scene_loader passed\n";
  return 0;
}
