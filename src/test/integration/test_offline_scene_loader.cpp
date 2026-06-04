#include "core/asset/builtin_meshes.hpp"
#include "core/scene/camera.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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

class ScopedFileMove {
public:
  ScopedFileMove(std::filesystem::path source, std::filesystem::path target)
      : m_source(std::move(source)), m_target(std::move(target)) {
    std::error_code error;
    if (std::filesystem::exists(m_target, error)) {
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__
                << " fixture hide target already exists: " << m_target
                << '\n';
      ++failures;
      return;
    }
    error.clear();
    if (!std::filesystem::exists(m_source, error)) {
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__
                << " fixture source missing before move: " << m_source
                << '\n';
      ++failures;
      return;
    }
    error.clear();
    std::filesystem::rename(m_source, m_target, error);
    m_moved = !error;
    if (error) {
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__
                << " failed to move fixture asset: " << error.message()
                << '\n';
      ++failures;
    }
  }

  ScopedFileMove(const ScopedFileMove &) = delete;
  ScopedFileMove &operator=(const ScopedFileMove &) = delete;

  ~ScopedFileMove() {
    if (!m_moved) {
      return;
    }
    std::error_code error;
    if (std::filesystem::exists(m_source, error)) {
      std::cerr << "[FAIL] ScopedFileMove restore target already exists: "
                << m_source << '\n';
      ++failures;
      return;
    }
    error.clear();
    std::filesystem::rename(m_target, m_source, error);
    if (error) {
      std::cerr << "[FAIL] ScopedFileMove failed to restore fixture asset: "
                << error.message() << '\n';
      ++failures;
    }
  }

  [[nodiscard]] bool moved() const { return m_moved; }

private:
  std::filesystem::path m_source;
  std::filesystem::path m_target;
  bool m_moved = false;
};

[[nodiscard]] std::string hiddenMaterialPrefix(
    const std::filesystem::path &materialPath) {
  return "." + materialPath.filename().string() +
         ".offline_explicit_test_hidden.";
}

void restoreStaleHiddenMaterialIfNeeded(
    const std::filesystem::path &materialPath) {
  std::error_code error;
  if (std::filesystem::exists(materialPath, error)) {
    return;
  }
  error.clear();
  const std::filesystem::path materialDir = materialPath.parent_path();
  const std::string prefix = hiddenMaterialPrefix(materialPath);
  for (const auto &entry :
       std::filesystem::directory_iterator(materialDir, error)) {
    if (error) {
      break;
    }
    const std::string filename = entry.path().filename().string();
    if (filename.rfind(prefix, 0) != 0) {
      continue;
    }
    std::filesystem::rename(entry.path(), materialPath, error);
    if (error) {
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__
                << " failed to restore stale hidden material: "
                << error.message() << '\n';
      ++failures;
    }
    return;
  }
}

[[nodiscard]] std::filesystem::path makeUniqueHiddenMaterialPath(
    const std::filesystem::path &materialPath) {
  const auto stamp = std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count();
  const std::filesystem::path materialDir = materialPath.parent_path();
  const std::string prefix = hiddenMaterialPrefix(materialPath);
  for (u32 attempt = 0; attempt < 1024u; ++attempt) {
    auto candidate =
        materialDir / (prefix + std::to_string(stamp) + "." +
                       std::to_string(attempt));
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) {
      return candidate;
    }
  }
  throw std::runtime_error("failed to create unique hidden material path");
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

LX_infra::scene_io::SceneDocument makePlainGltfHelmetDocument(
    std::optional<std::string> materialUri = std::nullopt) {
  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("offline plain glTF helmet");
  document.setGameplayCameraPath("/game_cam");

  auto &root = document.mutableRootNode();
  root.children.push_back(
      makeCameraNode("game_cam", LX_core::Vec3f{0.0f, 2.0f, 6.0f}, 45.0f,
                     0xffffffffu));

  LX_infra::scene_io::SceneNodeDocument helmet;
  helmet.nodeName = "helmet";
  helmet.name = "helmet";
  helmet.meshUri = "assets/models/damaged_helmet/DamagedHelmet.gltf";
  helmet.materialUri = std::move(materialUri);
  root.children.push_back(std::move(helmet));

  return document;
}

LX_infra::scene_io::SceneDocument makeTaggedGltfHelmetDocument(
    std::string offlineMaterialTag) {
  LX_infra::scene_io::SceneDocument document;
  document.setSceneName("offline tagged glTF helmet");
  document.setGameplayCameraPath("/game_cam");

  LX_core::offline::RenderProfileDocument profiles;
  auto preview = LX_core::offline::makeDefaultOutputProfile();
  preview.width = 64;
  preview.height = 64;
  preview.materialTag = "realtime-pbr";
  profiles.outputProfiles.emplace("preview", preview);
  profiles.defaultOutputProfile = "preview";
  profiles.offline = LX_core::offline::makeDefaultOfflineRenderSettings();
  profiles.offline.profileName = "preview";
  profiles.offline.materialTag = std::move(offlineMaterialTag);
  document.setRenderProfileDocument(std::move(profiles));

  auto &root = document.mutableRootNode();
  root.children.push_back(
      makeCameraNode("game_cam", LX_core::Vec3f{0.0f, 2.0f, 6.0f}, 45.0f,
                     0xffffffffu));

  LX_infra::scene_io::SceneNodeDocument helmet;
  helmet.nodeName = "helmet";
  helmet.name = "helmet";
  helmet.meshUri = "assets/models/damaged_helmet/DamagedHelmet.gltf";
  helmet.materials.push_back(LX_infra::scene_io::MaterialBindingDocument{
      .tag = "offline-pbr",
      .uri = "assets/materials/pbr.material",
      .source = "gltf",
  });
  helmet.materials.push_back(LX_infra::scene_io::MaterialBindingDocument{
      .tag = "realtime-blinnphong",
      .uri = "assets/materials/blinnphong_lit.material",
  });
  root.children.push_back(std::move(helmet));

  LX_infra::scene_io::SceneNodeDocument missingTagCube;
  missingTagCube.nodeName = "missing_tag_cube";
  missingTagCube.name = "missing_tag_cube";
  missingTagCube.transform.translation = {3.0f, 0.0f, 0.0f};
  missingTagCube.meshUri = "builtin://lxe_editor/primitives/cube";
  missingTagCube.materials.push_back(LX_infra::scene_io::MaterialBindingDocument{
      .tag = "realtime-blinnphong",
      .uri = "assets/materials/blinnphong_lit.material",
  });
  root.children.push_back(std::move(missingTagCube));

  return document;
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

void testPlainGltfHelmetLoadsToSceneResourceTable() {
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(std::filesystem::current_path() /
                                              "assets" / "scenes" /
                                              "warning_fixture.scene.yaml")};

  bool loadedWithoutBuiltinOnlyRejection = false;
  try {
    const auto loaded = loader.load(makePlainGltfHelmetDocument(), "/game_cam");
    loadedWithoutBuiltinOnlyRejection = true;
    EXPECT(loaded.table.objectCount() > 0,
           "plain glTF helmet should register an offline object");
    EXPECT(loaded.table.materialCount() > 0,
           "plain glTF helmet should register a shared PBR material");
  } catch (const std::exception &ex) {
    const std::string message = ex.what();
    EXPECT(message.find("offline MVP only supports shared builtin primitive "
                        "meshes") == std::string::npos,
           "plain glTF helmet should not hit the old builtin-only rejection");
  }
  EXPECT(loadedWithoutBuiltinOnlyRejection,
         "plain glTF helmet should load through OfflineSceneLoader");
}

void testOfflineSceneLoaderSelectsTaggedMaterialAndSkipsMissingTags() {
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(std::filesystem::current_path() /
                                              "assets" / "scenes" /
                                              "warning_fixture.scene.yaml")};

  const auto loaded =
      loader.load(makeTaggedGltfHelmetDocument("offline-pbr"), "/game_cam");
  const auto uploadView = loaded.table.buildUploadView();
  EXPECT(loaded.table.objectCount() == 1,
         "offline loader should skip objects without offline materialTag");
  EXPECT(loaded.table.materialCount() == 1,
         "offline loader should register only the selected tagged material");

  const auto hasPbrRecord = std::any_of(
      uploadView.materials.begin(), uploadView.materials.end(),
      [](const auto &material) {
        return nearly(material.pbrParams.x, 1.0f) &&
               nearly(material.pbrParams.y, 1.0f) &&
               nearly(material.pbrParams.w, 1.0f);
      });
  EXPECT(hasPbrRecord,
         "offline materialTag should select the configured glTF PBR material");
}

void testOfflineSceneLoaderCanSelectBlinnPhongTag() {
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(std::filesystem::current_path() /
                                              "assets" / "scenes" /
                                              "warning_fixture.scene.yaml")};

  const auto loaded = loader.load(
      makeTaggedGltfHelmetDocument("realtime-blinnphong"), "/game_cam");
  const auto uploadView = loaded.table.buildUploadView();
  const auto hasBlinnPhongRecord = std::any_of(
      uploadView.materials.begin(), uploadView.materials.end(),
      [](const auto &material) {
        return nearly(material.baseColor,
                      LX_core::Vec4f{0.8f, 0.8f, 0.8f, 1.0f}) &&
               nearly(material.pbrParams.z, 1.0f) &&
               nearly(material.emissive.w, 12.0f);
      });
  EXPECT(hasBlinnPhongRecord,
         "offline loader should use the selected non-PBR tagged material");
}

void testPlainGltfHelmetExplicitMaterialDoesNotLoadPbrBridgeMaterial() {
  const std::filesystem::path materialPath =
      std::filesystem::current_path() / "assets" / "materials" /
      "pbr.material";
  restoreStaleHiddenMaterialIfNeeded(materialPath);
  const std::filesystem::path hiddenMaterialPath =
      makeUniqueHiddenMaterialPath(materialPath);
  ScopedFileMove hidePbrMaterial(materialPath, hiddenMaterialPath);
  if (!hidePbrMaterial.moved()) {
    return;
  }

  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(std::filesystem::current_path() /
                                              "assets" / "scenes" /
                                              "warning_fixture.scene.yaml")};

  try {
    const auto loaded = loader.load(
        makePlainGltfHelmetDocument("assets/materials/blinnphong_lit.material"),
        "/game_cam");
    EXPECT(loaded.table.objectCount() > 0,
           "explicit-material glTF helmet should register an offline object");
    EXPECT(loaded.table.materialCount() > 0,
           "explicit-material glTF helmet should register a material");
    const auto uploadView = loaded.table.buildUploadView();
    const auto hasBlinnPhongRecord = std::any_of(
        uploadView.materials.begin(), uploadView.materials.end(),
        [](const auto &material) {
          return nearly(material.baseColor,
                        LX_core::Vec4f{0.8f, 0.8f, 0.8f, 1.0f}) &&
                 nearly(material.pbrParams.z, 1.0f) &&
                 nearly(material.emissive.w, 12.0f);
        });
    EXPECT(hasBlinnPhongRecord,
           "explicit glTF material should use blinnphong_lit material "
           "parameters instead of glTF PBR metadata");
  } catch (const std::exception &ex) {
    std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__
              << " explicit-material glTF helmet should not load hidden PBR "
                 "bridge material: "
              << ex.what() << '\n';
    ++failures;
  }
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
      EXPECT(camera->get().pose.eye == expectedPose.eye,
             "selected camera pose should be retained in CameraResource");
      EXPECT(camera->get().projection.type == LX_core::CameraType::Perspective,
             "selected camera projection type should be retained");
      EXPECT(nearly(camera->get().projection.fovYDegrees, 60.0f),
             "selected camera fov should be retained");
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

void testOrthographicCameraProjectionMetadataSurvivesLoading() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "warning_fixture.scene.yaml";
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(scenePath)};

  LX_infra::scene_io::SceneDocument document;
  document.setGameplayCameraPath("/ortho_cam");
  auto &root = document.mutableRootNode();
  LX_infra::scene_io::SceneNodeDocument cameraNode;
  cameraNode.name = "ortho_cam";
  cameraNode.transform.translation = {1.0f, 2.0f, 3.0f};
  cameraNode.camera = LX_infra::scene_io::CameraNodeState{
      .type = LX_core::CameraType::Orthographic,
      .aspect = 1.5f,
      .nearPlane = 0.5f,
      .farPlane = 80.0f,
      .orthographicHeight = 6.0f,
      .cullingMask = 0x00000007u,
  };
  root.children.push_back(std::move(cameraNode));

  LX_infra::scene_io::SceneNodeDocument cube;
  cube.name = "cube";
  cube.meshUri = "builtin://lxe_editor/primitives/cube";
  root.children.push_back(std::move(cube));

  auto loaded = loader.load(document, "/ortho_cam");
  const auto snapshot = loaded.table.buildSnapshot();
  EXPECT(snapshot.cameraHandles.size() == 1,
         "orthographic fixture should register one camera");
  if (snapshot.cameraHandles.empty()) {
    return;
  }
  const auto camera = loaded.table.resolve(snapshot.cameraHandles.front());
  EXPECT(camera.has_value(), "orthographic camera handle should resolve");
  if (!camera.has_value()) {
    return;
  }

  const auto &projection = camera->get().projection;
  EXPECT(projection.type == LX_core::CameraType::Orthographic,
         "CameraResource should preserve orthographic projection type");
  EXPECT(nearly(projection.aspect, 1.5f),
         "CameraResource should preserve orthographic aspect");
  EXPECT(nearly(projection.left, -4.5f) && nearly(projection.right, 4.5f) &&
             nearly(projection.bottom, -3.0f) &&
             nearly(projection.top, 3.0f),
         "CameraResource should preserve orthographic bounds from height");
  const LX_core::Vec3f expectedEye{1.0f, 2.0f, 3.0f};
  EXPECT(camera->get().pose.eye == expectedEye,
         "CameraResource should preserve camera pose");
}

} // namespace

int main() {
  testIblMetalSphereLoadsToSceneResourceTable();
  testBuiltinSphereUsesSharedPrimitiveMesh();
  testPlainGltfHelmetLoadsToSceneResourceTable();
  testOfflineSceneLoaderSelectsTaggedMaterialAndSkipsMissingTags();
  testOfflineSceneLoaderCanSelectBlinnPhongTag();
  testPlainGltfHelmetExplicitMaterialDoesNotLoadPbrBridgeMaterial();
  testBuiltinSphereWindingMatchesOutwardNormals();
  testSelectedCameraObjectStateAndWarnings();
  testOrthographicCameraProjectionMetadataSurvivesLoading();
  if (failures != 0) {
    std::cerr << "test_offline_scene_loader failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_scene_loader passed\n";
  return 0;
}
