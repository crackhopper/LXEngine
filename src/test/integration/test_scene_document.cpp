#include "demos/scene_viewer/scene_document.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace demo = LX_demo::scene_viewer;

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

[[nodiscard]] std::filesystem::path makeTempPath(const char* filename) {
  return std::filesystem::temp_directory_path() / filename;
}

void testLoadSceneDocumentReadsGameAndEditorCamera() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_load.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: scene_viewer\n"
         "  gameplayCameraPath: /world/game_cam\n"
         "nodes:\n"
         "  - nodeName: world_root\n"
         "    name: world\n"
         "    transform:\n"
         "      translation: [0.0, 0.0, 0.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "  - nodeName: game_camera\n"
         "    name: game_cam\n"
         "    parentPath: /world\n"
         "    transform:\n"
         "      translation: [0.0, 2.0, 6.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "    camera:\n"
         "      eye: [0.0, 2.0, 6.0]\n"
         "      target: [0.0, 0.0, 0.0]\n"
         "      up: [0.0, 1.0, 0.0]\n"
         "      type: perspective\n"
         "      fovY: 45.0\n"
         "      aspect: 1.7777778\n"
         "      nearPlane: 0.1\n"
         "      farPlane: 1000.0\n"
         "      left: -1.0\n"
         "      right: 1.0\n"
         "      bottom: -1.0\n"
         "      top: 1.0\n"
         "      cullingMask: 4294967295\n"
         "  - nodeName: ground\n"
         "    name: ground\n"
         "    parentPath: /world\n"
         "    transform:\n"
         "      translation: [0.0, -1.5, 0.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "    mesh:\n"
         "      uri: builtin://scene_viewer/ground_mesh\n"
         "    material:\n"
         "      uri: builtin://scene_viewer/ground_material\n"
         "  - nodeName: dir_light_node\n"
         "    name: dir_light\n"
         "    parentPath: /world\n"
         "    transform:\n"
         "      translation: [0.0, 0.0, 0.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "    directionalLight:\n"
         "      direction: [-0.3, -1.0, -0.5]\n"
         "      color: [1.0, 0.98, 0.9]\n"
         "      intensity: 1.0\n"
         "editor:\n"
         "  editorCamera:\n"
         "    position: [5.0, 6.0, 7.0]\n"
         "    rotationEulerDeg: [0.0, 90.0, 0.0]\n"
         "    fovY: 35.0\n"
         "    nearPlane: 0.2\n"
         "    farPlane: 400.0\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.sceneName() == "scene_viewer", "scene name should load");
  EXPECT(doc.gameplayCameraPath() == "/world/game_cam",
         "gameplay camera path should load");
  EXPECT(doc.nodes().size() == 4, "scene nodes should load");
  EXPECT(doc.nodes()[1].camera.has_value(), "camera node should load");
  EXPECT(doc.nodes()[1].camera->eye.y == 2.0f, "camera eye should load");
  EXPECT(doc.nodes()[2].meshUri.has_value(), "mesh uri should load");
  EXPECT(*doc.nodes()[2].meshUri == "builtin://scene_viewer/ground_mesh",
         "mesh uri should survive load");
  EXPECT(doc.nodes()[3].directionalLight.has_value(),
         "directional light should load");
  EXPECT(doc.nodes()[3].directionalLight->color.y == 0.98f,
         "directional light color should load");
  EXPECT(doc.hasEditorCamera(), "editor camera metadata should load");
  EXPECT(doc.editorCamera().position.x == 5.0f, "editor camera x should load");
  EXPECT(doc.editorCamera().rotationEulerDeg.y == 90.0f,
         "editor camera yaw should load");
  EXPECT(doc.editorCamera().fovY == 35.0f, "editor camera fov should load");
  EXPECT(doc.editorCamera().nearPlane == 0.2f,
         "editor camera near plane should load");
  EXPECT(doc.editorCamera().farPlane == 400.0f,
         "editor camera far plane should load");
}

void testSaveSceneDocumentRoundTripsEditorMetadata() {
  demo::SceneDocument doc;
  doc.setSceneName("scene_viewer");
  doc.setGameplayCameraPath("/world/game_cam");
  auto& nodes = doc.mutableNodes();
  nodes.push_back(demo::SceneNodeDocument{
      .nodeName = "world_root",
      .name = "world",
  });
  nodes.push_back(demo::SceneNodeDocument{
      .nodeName = "game_camera",
      .name = "game_cam",
      .parentPath = "/world",
      .transform = {
          .translation = {1.0f, 2.0f, 3.0f},
          .rotation = LX_core::Quatf{1.0f, 0.0f, 0.0f, 0.0f},
          .scale = {1.0f, 1.0f, 1.0f},
      },
      .camera =
          demo::CameraNodeState{
              .eye = {1.0f, 2.0f, 3.0f},
              .target = {0.0f, 0.0f, 0.0f},
              .up = {0.0f, 1.0f, 0.0f},
              .fovY = 55.0f,
              .aspect = 1.5f,
              .nearPlane = 0.5f,
              .farPlane = 250.0f,
          },
  });
  nodes.push_back(demo::SceneNodeDocument{
      .nodeName = "helmet",
      .name = "helmet",
      .parentPath = "/world",
      .meshUri = std::string("assets/models/damaged_helmet/DamagedHelmet.gltf"),
      .materialUri = std::string("assets/materials/blinnphong_textured.material"),
  });
  nodes.push_back(demo::SceneNodeDocument{
      .nodeName = "dir_light_node",
      .name = "dir_light",
      .parentPath = "/world",
      .directionalLight =
          demo::DirectionalLightNodeState{
              .direction = {-0.3f, -1.0f, -0.5f},
              .color = {1.0f, 0.98f, 0.9f},
              .intensity = 2.0f,
          },
  });
  doc.setEditorCamera(demo::EditorCameraState{
      .position = {7.0f, 8.0f, 9.0f},
      .rotationEulerDeg = {10.0f, 20.0f, 30.0f},
      .fovY = 35.0f,
      .nearPlane = 0.2f,
      .farPlane = 400.0f,
  });

  const std::filesystem::path path =
      makeTempPath("lx_scene_document_roundtrip.yaml");
  demo::saveSceneDocument(path, doc);

  const demo::SceneDocument loaded = demo::loadSceneDocument(path);
  EXPECT(loaded.sceneName() == "scene_viewer",
         "scene name should survive round trip");
  EXPECT(loaded.gameplayCameraPath() == "/world/game_cam",
         "gameplay camera path should survive round trip");
  EXPECT(loaded.nodes().size() == 4, "nodes should survive round trip");
  EXPECT(loaded.nodes()[1].camera.has_value(),
         "camera node should survive round trip");
  EXPECT(loaded.nodes()[1].camera->eye.x == 1.0f,
         "camera eye should survive round trip");
  EXPECT(loaded.nodes()[1].camera->nearPlane == 0.5f,
         "camera near plane should survive round trip");
  EXPECT(loaded.nodes()[2].meshUri.has_value(),
         "mesh uri should survive round trip");
  EXPECT(*loaded.nodes()[2].meshUri ==
             "assets/models/damaged_helmet/DamagedHelmet.gltf",
         "mesh uri should round trip");
  EXPECT(loaded.nodes()[2].materialUri.has_value(),
         "material uri should survive round trip");
  EXPECT(loaded.nodes()[3].directionalLight.has_value(),
         "directional light should survive round trip");
  EXPECT(loaded.nodes()[3].directionalLight->intensity == 2.0f,
         "directional light intensity should survive round trip");
  EXPECT(loaded.hasEditorCamera(),
         "editor camera metadata should survive round trip");
  EXPECT(loaded.editorCamera().position.z == 9.0f,
         "editor camera position should survive round trip");
  EXPECT(loaded.editorCamera().rotationEulerDeg.x == 10.0f,
         "editor camera rotation should survive round trip");
  EXPECT(loaded.editorCamera().fovY == 35.0f,
         "editor camera fov should survive round trip");
  EXPECT(loaded.editorCamera().nearPlane == 0.2f,
         "editor camera near plane should survive round trip");
  EXPECT(loaded.editorCamera().farPlane == 400.0f,
         "editor camera far plane should survive round trip");
}

} // namespace

int main() {
  testLoadSceneDocumentReadsGameAndEditorCamera();
  testSaveSceneDocumentRoundTripsEditorMetadata();

  if (failures != 0) {
    std::cerr << "test_scene_document failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_document passed\n";
  return 0;
}
