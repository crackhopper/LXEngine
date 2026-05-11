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
         "gameCamera:\n"
         "  eye: [0.0, 2.0, 6.0]\n"
         "  target: [0.0, 0.0, 0.0]\n"
         "  up: [0.0, 1.0, 0.0]\n"
         "  fovY: 45.0\n"
         "  nearPlane: 0.1\n"
         "  farPlane: 1000.0\n"
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
  EXPECT(doc.hasEditorCamera(), "editor camera metadata should load");
  EXPECT(doc.editorCamera().position.x == 5.0f, "editor camera x should load");
  EXPECT(doc.editorCamera().rotationEulerDeg.y == 90.0f,
         "editor camera yaw should load");
  EXPECT(doc.editorCamera().fovY == 35.0f, "editor camera fov should load");
  EXPECT(doc.editorCamera().nearPlane == 0.2f,
         "editor camera near plane should load");
  EXPECT(doc.editorCamera().farPlane == 400.0f,
         "editor camera far plane should load");
  EXPECT(doc.gameCamera().eye.y == 2.0f, "game camera eye should load");
  EXPECT(doc.gameCamera().farPlane == 1000.0f,
         "game camera far plane should load");
}

void testSaveSceneDocumentRoundTripsEditorMetadata() {
  demo::SceneDocument doc;
  doc.setSceneName("scene_viewer");
  doc.mutableGameCamera().eye = {1.0f, 2.0f, 3.0f};
  doc.mutableGameCamera().target = {0.0f, 0.0f, 0.0f};
  doc.mutableGameCamera().up = {0.0f, 1.0f, 0.0f};
  doc.mutableGameCamera().fovY = 55.0f;
  doc.mutableGameCamera().nearPlane = 0.5f;
  doc.mutableGameCamera().farPlane = 250.0f;
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
  EXPECT(loaded.gameCamera().eye.x == 1.0f,
         "game camera eye should survive round trip");
  EXPECT(loaded.gameCamera().nearPlane == 0.5f,
         "game camera near plane should survive round trip");
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
