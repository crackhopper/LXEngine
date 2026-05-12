#include "core/scene/components/camera_component.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"
#include "demos/lxe_editor/scene_document.hpp"
#include "demos/lxe_editor/scene_runtime.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>

namespace demo = LX_demo::lxe_editor;

namespace {

static_assert(!std::is_copy_constructible_v<demo::SceneRuntime>,
              "SceneRuntime should be move-only");
static_assert(!std::is_copy_assignable_v<demo::SceneRuntime>,
              "SceneRuntime should be move-only");

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

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void writeSceneFile(const std::filesystem::path& path,
                    const std::string& body) {
  std::ofstream out(path);
  out << body;
}

void expectNear(const float lhs, const float rhs, const char* msg,
                const float epsilon = 0.001f) {
  if (std::abs(lhs - rhs) > epsilon) {
    std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg
              << " (" << lhs << " vs " << rhs << ")\n";
    ++failures;
  }
}

void testRuntimeCreatesEmptyScene() {
  demo::SceneRuntime runtime;
  runtime.createEmptyScene();

  EXPECT(runtime.scene(), "empty runtime should create a scene");
  EXPECT(runtime.scene()->findByPath("/") == runtime.scene()->getRootNode().get(),
         "empty runtime should expose the explicit scene root at slash");
  EXPECT(!runtime.documentPath().has_value(),
         "empty runtime should not have a document path");
  EXPECT(runtime.gameCameraNode(), "empty runtime should have a gameplay camera");
  EXPECT(runtime.editorCameraNode(), "empty runtime should have an editor camera");
  EXPECT(runtime.gameCameraNode()->getParent() == runtime.scene()->getRootNode(),
         "gameplay camera should attach under the scene root");
  EXPECT(runtime.editorCameraNode()->getParent() == runtime.scene()->getRootNode(),
         "editor camera should attach under the scene root");
  EXPECT(runtime.scene()->findByPath("/helmet") == nullptr,
         "empty runtime should not create a helmet node");
  EXPECT(runtime.scene()->findByPath("/ground") == nullptr,
         "empty runtime should not create a ground node");
  EXPECT(runtime.scene()->getRenderables().size() == 2,
         "empty runtime should contain only editor and gameplay camera nodes");
}

void testRuntimeLoadsFullSceneDocument() {
  const std::filesystem::path path = makeTempPath("lx_scene_runtime_full.yaml");
  writeSceneFile(path,
                 "scene:\n"
                 "  name: sample_scene\n"
                 "  gameplayCameraPath: /world/game_cam\n"
                 "root:\n"
                 "  nodeName: scene_root\n"
                 "  name: ''\n"
                 "  transform:\n"
                 "    translation: [0.0, 0.0, 0.0]\n"
                 "    rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "    scale: [1.0, 1.0, 1.0]\n"
                 "  visibilityMask: 4294967295\n"
                 "  children:\n"
                 "    - nodeName: world_root\n"
                 "      name: world\n"
                 "      transform:\n"
                 "        translation: [0.0, 0.0, 0.0]\n"
                 "        rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "        scale: [1.0, 1.0, 1.0]\n"
                 "      visibilityMask: 4294967295\n"
                 "      children:\n"
                 "        - nodeName: game_camera\n"
                 "          name: game_cam\n"
                 "          transform:\n"
                 "            translation: [0.0, 2.0, 6.0]\n"
                 "            rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "            scale: [1.0, 1.0, 1.0]\n"
                 "          visibilityMask: 4294967295\n"
                 "          camera:\n"
                 "            eye: [0.0, 2.0, 6.0]\n"
                 "            target: [0.0, 0.0, 0.0]\n"
                 "            up: [0.0, 1.0, 0.0]\n"
                 "            type: perspective\n"
                 "            fovY: 45.0\n"
                 "            aspect: 1.7777778\n"
                 "            nearPlane: 0.1\n"
                 "            farPlane: 1000.0\n"
                 "            left: -1.0\n"
                 "            right: 1.0\n"
                 "            bottom: -1.0\n"
                 "            top: 1.0\n"
                 "            cullingMask: 4294967295\n"
                 "        - nodeName: ground\n"
                 "          name: ground\n"
                 "          transform:\n"
                 "            translation: [0.0, 0.0, 0.0]\n"
                 "            rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "            scale: [1.0, 1.0, 1.0]\n"
                 "          visibilityMask: 4294967295\n"
                 "          mesh:\n"
                 "            uri: builtin://lxe_editor/ground_mesh\n"
                 "          material:\n"
                 "            uri: builtin://lxe_editor/ground_material\n"
                 "        - nodeName: dir_light_node\n"
                 "          name: dir_light\n"
                 "          transform:\n"
                 "            translation: [0.0, 0.0, 0.0]\n"
                 "            rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "            scale: [1.0, 1.0, 1.0]\n"
                 "          visibilityMask: 4294967295\n"
                 "          directionalLight:\n"
                 "            direction: [-0.3, -1.0, -0.5]\n"
                 "            color: [1.0, 0.98, 0.9]\n"
                 "            intensity: 1.0\n"
                 "editor:\n"
                 "  editorCamera:\n"
                 "    position: [5.0, 6.0, 7.0]\n"
                 "    rotationEulerDeg: [0.0, 90.0, 0.0]\n"
                 "    fovY: 35.0\n"
                 "    nearPlane: 0.2\n"
                 "    farPlane: 400.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.scene(), "scene should exist after load");
  EXPECT(runtime.documentPath().has_value(), "loaded runtime should track path");
  EXPECT(runtime.scene()->findByPath("/") == runtime.scene()->getRootNode().get(),
         "loaded runtime should resolve slash to explicit root");
  EXPECT(runtime.scene()->findByPath("/world") != nullptr,
         "scene should load the root node");
  EXPECT(runtime.scene()->findByPath("/world")->getParent().get() ==
             runtime.scene()->getRootNode().get(),
         "top-level world node should be parented to the explicit root");
  EXPECT(runtime.scene()->findByPath("/world/ground") != nullptr,
         "scene should load renderable nodes");
  EXPECT(runtime.scene()->findByPath("/world/dir_light") != nullptr,
         "scene should load light placeholder nodes");
  EXPECT(runtime.scene()->findByPath("/world/game_cam") ==
             runtime.gameCameraNode().get(),
         "gameplay camera path should resolve to runtime gameplay camera");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.x == 5.0f,
         "editor metadata should restore editor camera x");
  const auto editorCamera =
      runtime.editorCameraNode()->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  if (editorCamera.has_value()) {
    expectNear(editorCamera->get().getFovY(), 35.0f,
               "editor metadata should restore editor camera fov");
    expectNear(editorCamera->get().getNearPlane(), 0.2f,
               "editor metadata should restore editor camera near");
    expectNear(editorCamera->get().getFarPlane(), 400.0f,
               "editor metadata should restore editor camera far");
  }
}

void testRuntimeLoadsLegacyFlatSceneDocumentWithExplicitRootNormalization() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_legacy.yaml");
  writeSceneFile(path,
                 "scene:\n"
                 "  name: legacy_scene\n"
                 "  gameplayCameraPath: /node_world/game_camera\n"
                 "nodes:\n"
                 "  - nodeName: node_world\n"
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
                 "      cullingMask: 4294967295\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.scene()->findByPath("/") == runtime.scene()->getRootNode().get(),
         "legacy runtime should still expose explicit root");
  EXPECT(runtime.scene()->findByPath("/world") != nullptr,
         "legacy world should still load");
  EXPECT(runtime.scene()->findByPath("/world/game_cam") ==
             runtime.gameCameraNode().get(),
         "legacy nodeName-based gameplay camera path should still resolve");
}

void testRuntimeSaveRoundTripsExpandedSceneDocument() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_save_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_save_output.yaml");
  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: sample_scene\n"
                 "  gameplayCameraPath: /game_cam\n"
                 "nodes:\n"
                 "  - nodeName: game_camera\n"
                 "    name: game_cam\n"
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
                 "      cullingMask: 4294967295\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(inputPath);

  auto editorCamera =
      runtime.editorCameraNode()->getComponent<LX_core::CameraComponent>();
  auto gameCamera =
      runtime.gameCameraNode()->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera should exist before save");
  EXPECT(gameCamera.has_value(), "game camera should exist before save");
  if (!editorCamera.has_value() || !gameCamera.has_value()) {
    return;
  }

  demo::EditorCameraState{
      .position = LX_core::Vec3f{4.0f, 5.0f, 6.0f},
      .rotationEulerDeg = LX_core::Vec3f{10.0f, 20.0f, 30.0f},
      .fovY = 35.0f,
      .nearPlane = 0.2f,
      .farPlane = 400.0f,
  }.applyTo(*runtime.editorCameraNode(), editorCamera->get());

  gameCamera->get().lookAt(LX_core::Vec3f{7.0f, 8.0f, 9.0f},
                           LX_core::Vec3f{7.0f, 8.0f, 2.0f},
                           LX_core::Vec3f{0.0f, 1.0f, 0.0f});
  gameCamera->get().setFovY(60.0f);
  gameCamera->get().setNearPlane(0.5f);
  gameCamera->get().setFarPlane(250.0f);

  runtime.saveToDocumentPath(savePath);

  const std::string savedText = readFile(savePath);
  EXPECT(savedText.find("\nroot:\n") != std::string::npos,
         "save should write canonical explicit-root format");
  EXPECT(savedText.find("\nnodes:\n") == std::string::npos,
         "save should stop writing legacy flat nodes");

  const demo::SceneDocument saved = demo::loadSceneDocument(savePath);
  EXPECT(saved.sceneName() == "sample_scene",
         "save should persist scene name");
  EXPECT(saved.gameplayCameraPath() == "/game_cam",
         "save should persist gameplay camera path");
  EXPECT(saved.hasEditorCamera(),
         "save should persist editor camera metadata");
  EXPECT(saved.rootNode().children.size() == 1,
         "save should persist root child hierarchy");
  EXPECT(saved.rootNode().children[0].camera.has_value(),
         "save should persist runtime gameplay camera state");
  expectNear(saved.rootNode().children[0].camera->eye.x, 7.0f,
             "save should persist camera eye x");
  expectNear(saved.rootNode().children[0].camera->target.z, 2.0f,
             "save should persist exact camera target z");
  expectNear(saved.editorCamera().position.x, 4.0f,
             "save should persist editor camera x");
}

} // namespace

int main() {
  testRuntimeCreatesEmptyScene();
  testRuntimeLoadsFullSceneDocument();
  testRuntimeLoadsLegacyFlatSceneDocumentWithExplicitRootNormalization();
  testRuntimeSaveRoundTripsExpandedSceneDocument();

  if (failures != 0) {
    std::cerr << "test_scene_runtime failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_runtime passed\n";
  return 0;
}
