#include "core/asset/builtin_meshes.hpp"
#include "core/scene/camera.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <algorithm>
#include <cmath>
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

bool nearly(float lhs, float rhs, float epsilon = 1.0e-4f) {
  return std::abs(lhs - rhs) <= epsilon;
}

bool nearly(const LX_core::Vec4f &lhs, const LX_core::Vec4f &rhs,
            float epsilon = 1.0e-4f) {
  return nearly(lhs.x, rhs.x, epsilon) && nearly(lhs.y, rhs.y, epsilon) &&
         nearly(lhs.z, rhs.z, epsilon) && nearly(lhs.w, rhs.w, epsilon);
}

bool matrixNearly(const LX_core::Mat4f &lhs, const LX_core::Mat4f &rhs,
                  float epsilon = 1.0e-4f) {
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!nearly(lhs(row, col), rhs(row, col), epsilon)) {
        return false;
      }
    }
  }
  return true;
}

LX_infra::scene_io::SceneNodeDocument makeCameraNode(
    std::string name, LX_core::Vec3f translation, float fovY,
    LX_core::VisibilityLayerMask cullingMask) {
  LX_infra::scene_io::SceneNodeDocument node;
  node.name = std::move(name);
  node.transform.translation = translation;
  node.camera = LX_infra::scene_io::CameraNodeState{
      .type = LX_core::CameraType::Perspective,
      .fovY = fovY,
      .aspect = 2.0f,
      .nearPlane = 0.25f,
      .farPlane = 50.0f,
      .cullingMask = cullingMask,
  };
  return node;
}

LX_infra::scene_io::SceneDocument makeWarningSceneDocument() {
  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("offline loader warning fixture");
  document.setGameplayCameraPath("/cam_a");

  auto &root = document.mutableRootNode();
  root.children.push_back(
      makeCameraNode("cam_a", LX_core::Vec3f{0.0f, 0.0f, 3.0f}, 30.0f,
                     0x00000001u));
  root.children.push_back(
      makeCameraNode("cam_b", LX_core::Vec3f{0.0f, 0.0f, 8.0f}, 60.0f,
                     0x12345678u));

  LX_infra::scene_io::SceneNodeDocument cube;
  cube.name = "missing_material_cube";
  cube.transform.translation = {2.0f, 3.0f, 4.0f};
  cube.transform.scale = {2.0f, 2.0f, 2.0f};
  cube.visibilityMask = 0x12345678u;
  cube.meshUri = "builtin://lxe_editor/primitives/cube";
  cube.materialUri = "assets/materials/does_not_exist.material";
  root.children.push_back(std::move(cube));

  return document;
}

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
  EXPECT(table.lightCount() == 1, "scene table should contain directional light");
  EXPECT(!table.buildUploadView().primitives.empty(),
         "scene table should produce upload primitives");

  const auto uploadView = table.buildUploadView();
  const auto hasGroundOverride = std::any_of(
      uploadView.materials.begin(), uploadView.materials.end(),
      [](const auto &material) {
        return nearly(material.baseColor,
                      LX_core::Vec4f{0.46f, 0.48f, 0.50f, 1.0f});
      });
  EXPECT(hasGroundOverride,
         "node material baseColor override should reach GPU material records");

  const auto hasGoldPbr = std::any_of(
      uploadView.materials.begin(), uploadView.materials.end(),
      [](const auto &material) {
        return nearly(material.pbrParams.x, 1.0f) &&
               nearly(material.pbrParams.y, 0.25f);
      });
  EXPECT(hasGoldPbr,
         "material asset metallic/roughness values should reach GPU records");
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

void testSelectedCameraObjectStateAndWarnings() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "warning_fixture.scene.yaml";
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto document = makeWarningSceneDocument();
  auto loaded = loader.load(document, "/cam_b");
  auto &table = loaded.table;

  EXPECT(table.cameraCount() == 1, "only the selected camera should load");
  EXPECT(table.lightCount() == 0, "fixture should not register lights");
  EXPECT(table.objectCount() == 1, "fixture should load one object");

  const auto hasMissingMaterialWarning = std::any_of(
      loaded.warnings.begin(), loaded.warnings.end(), [](const auto &warning) {
        return warning.find("material asset not found") != std::string::npos;
      });
  EXPECT(hasMissingMaterialWarning,
         "missing material asset should produce a warning");
  const auto hasNoDirectionalLightWarning = std::any_of(
      loaded.warnings.begin(), loaded.warnings.end(), [](const auto &warning) {
        return warning.find("no directional light") != std::string::npos;
      });
  EXPECT(hasNoDirectionalLightWarning,
         "scene without directional light should produce a warning");

  const auto snapshot = table.buildSnapshot();
  EXPECT(snapshot.cameraHandles.size() == 1,
         "snapshot should contain selected camera handle");
  if (!snapshot.cameraHandles.empty()) {
    const auto camera = table.resolve(snapshot.cameraHandles.front());
    EXPECT(camera.has_value(), "selected camera handle should resolve");
    if (camera.has_value()) {
      const auto expectedPose = LX_core::makeCameraPose(
          LX_core::Vec3f{0.0f, 0.0f, 8.0f},
          LX_core::Vec3f{0.0f, 0.0f, -1.0f},
          LX_core::Vec3f{0.0f, 1.0f, 0.0f});
      const LX_core::CameraProjection expectedProjection{
          .type = LX_core::CameraType::Perspective,
          .fovYDegrees = 60.0f,
          .aspect = 2.0f,
          .nearPlane = 0.25f,
          .farPlane = 50.0f,
      };
      EXPECT(matrixNearly(camera->get().view,
                          LX_core::makeCameraViewMatrix(expectedPose)),
             "selected camera view matrix should match /cam_b transform");
      EXPECT(matrixNearly(
                 camera->get().proj,
                 LX_core::makeCameraProjectionMatrix(expectedProjection)),
             "selected camera projection matrix should match /cam_b settings");
      EXPECT(camera->get().cullingMask == 0x12345678u,
             "selected camera culling mask should load");
    }
  }

  EXPECT(snapshot.objects.size() == 1, "snapshot should contain one object");
  if (!snapshot.objects.empty()) {
    const auto &object = snapshot.objects.front();
    EXPECT(object.visibilityMask == 0x12345678u,
           "object visibility mask should load");
    EXPECT(object.worldBounds.isValid(), "object world bounds should be valid");
    const LX_core::Vec3f expectedMin{1.0f, 2.0f, 3.0f};
    const LX_core::Vec3f expectedMax{3.0f, 4.0f, 5.0f};
    EXPECT(object.worldBounds.min == expectedMin &&
               object.worldBounds.max == expectedMax,
           "object world bounds should include transform scale and translation");
  }

  const auto uploadView = table.buildUploadView();
  EXPECT(uploadView.objects.size() == 1,
         "upload view should contain one object record");
  if (!uploadView.objects.empty()) {
    const auto &object = uploadView.objects.front();
    EXPECT(nearly(object.objectToWorld[3].x, 2.0f) &&
               nearly(object.objectToWorld[3].y, 3.0f) &&
               nearly(object.objectToWorld[3].z, 4.0f) &&
               nearly(object.objectToWorld[3].w, 1.0f),
           "objectToWorld GPU record should carry translation");
    EXPECT(object.visibilityMask == 0x12345678u,
           "object visibility mask should reach GPU record");
  }
}

} // namespace

int main() {
  testIblMetalSphereLoadsToSceneResourceTable();
  testBuiltinSphereUsesSharedPrimitiveMesh();
  testBuiltinSphereWindingMatchesOutwardNormals();
  testSelectedCameraObjectStateAndWarnings();
  if (failures != 0) {
    std::cerr << "test_offline_scene_loader failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_scene_loader passed\n";
  return 0;
}
