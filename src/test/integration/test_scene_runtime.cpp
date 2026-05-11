#include "core/scene/components/camera_component.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/scene_viewer/editor_camera_state.hpp"
#include "demos/scene_viewer/scene_document.hpp"
#include "demos/scene_viewer/scene_runtime.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>

namespace demo = LX_demo::scene_viewer;

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

[[nodiscard]] std::filesystem::path normalizePath(
    const std::filesystem::path& path) {
  return std::filesystem::absolute(path).lexically_normal();
}

void writeSceneFile(const std::filesystem::path& path,
                    const std::string& body) {
  std::ofstream out(path);
  out << body;
  out.close();
}

void expectNear(const float lhs, const float rhs, const char* msg,
                const float epsilon = 0.001f) {
  if (std::abs(lhs - rhs) > epsilon) {
    std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg
              << " (" << lhs << " vs " << rhs << ")\n";
    ++failures;
  }
}

void testRuntimeFallsBackEditorCameraFromGameCamera() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_fallback.yaml");
  writeSceneFile(path,
                 "scene:\n"
                 "  name: scene_viewer\n"
                 "gameCamera:\n"
                 "  eye: [0.0, 2.0, 6.0]\n"
                 "  target: [0.0, 0.0, 0.0]\n"
                 "  up: [0.0, 1.0, 0.0]\n"
                 "  fovY: 45.0\n"
                 "  nearPlane: 0.1\n"
                 "  farPlane: 1000.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.scene(), "scene should exist after load");
  EXPECT(runtime.editorCameraNode(), "editor camera node should exist");
  EXPECT(runtime.gameCameraNode(), "game camera node should exist");
  const auto editorCamera =
      runtime.editorCameraNode()->getComponent<LX_core::CameraComponent>();
  const auto gameCamera =
      runtime.gameCameraNode()->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  EXPECT(gameCamera.has_value(), "game camera component should exist");
  EXPECT(editorCamera->get().matchesTarget(LX_core::RenderTarget{}),
         "editor camera should target the default render target");
  EXPECT(gameCamera->get().matchesTarget(LX_core::RenderTarget{}),
         "game camera should target the default render target");
  EXPECT(editorCamera->get().getCullingMask() == LX_core::Layer_All,
         "editor camera should include editor overlay visibility");
  EXPECT(gameCamera->get().getCullingMask() ==
             (LX_core::Layer_All & ~LX_core::Layer_EditorOverlay),
         "game camera should exclude editor overlay visibility");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.x ==
             runtime.gameCameraNode()->getLocalTransform().translation.x,
         "missing editor metadata should copy game camera x");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.y ==
             runtime.gameCameraNode()->getLocalTransform().translation.y,
         "missing editor metadata should copy game camera y");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.z ==
             runtime.gameCameraNode()->getLocalTransform().translation.z,
         "missing editor metadata should copy game camera z");
  EXPECT(editorCamera->get().fovY == gameCamera->get().fovY,
         "missing editor metadata should copy game camera fov");
  EXPECT(editorCamera->get().nearPlane == gameCamera->get().nearPlane,
         "missing editor metadata should copy game camera near plane");
  EXPECT(editorCamera->get().farPlane == gameCamera->get().farPlane,
         "missing editor metadata should copy game camera far plane");
  expectNear(editorCamera->get().getLookTarget().x,
             gameCamera->get().getLookTarget().x,
             "missing editor metadata should preserve game camera target x");
  expectNear(editorCamera->get().getLookTarget().y,
             gameCamera->get().getLookTarget().y,
             "missing editor metadata should preserve game camera target y");
  expectNear(editorCamera->get().getLookTarget().z,
             gameCamera->get().getLookTarget().z,
             "missing editor metadata should preserve game camera target z");
  expectNear(editorCamera->get().getUpVector().x,
             gameCamera->get().getUpVector().x,
             "missing editor metadata should preserve game camera up x");
  expectNear(editorCamera->get().getUpVector().y,
             gameCamera->get().getUpVector().y,
             "missing editor metadata should preserve game camera up y");
  expectNear(editorCamera->get().getUpVector().z,
             gameCamera->get().getUpVector().z,
             "missing editor metadata should preserve game camera up z");
}

void testRuntimeRestoresEditorCameraMetadataWhenPresent() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_editor_restore.yaml");
  writeSceneFile(path,
                 "scene:\n"
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
                 "    position: [9.0, 8.0, 7.0]\n"
                 "    rotationEulerDeg: [0.0, 90.0, 0.0]\n"
                 "    fovY: 35.0\n"
                 "    nearPlane: 0.2\n"
                 "    farPlane: 400.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.editorCameraNode(), "editor camera node should exist");
  EXPECT(runtime.gameCameraNode(), "game camera node should exist");
  const auto editorCamera =
      runtime.editorCameraNode()->getComponent<LX_core::CameraComponent>();
  const auto gameCamera =
      runtime.gameCameraNode()->getComponent<LX_core::CameraComponent>();
  EXPECT(editorCamera.has_value(), "editor camera component should exist");
  EXPECT(gameCamera.has_value(), "game camera component should exist");
  EXPECT(editorCamera->get().matchesTarget(LX_core::RenderTarget{}),
         "editor camera should target the default render target");
  EXPECT(gameCamera->get().matchesTarget(LX_core::RenderTarget{}),
         "game camera should target the default render target");
  EXPECT(editorCamera->get().getCullingMask() == LX_core::Layer_All,
         "editor camera should include editor overlay visibility");
  EXPECT(gameCamera->get().getCullingMask() ==
             (LX_core::Layer_All & ~LX_core::Layer_EditorOverlay),
         "game camera should exclude editor overlay visibility");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.x == 9.0f,
         "editor metadata should restore editor camera x");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.y == 8.0f,
         "editor metadata should restore editor camera y");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.z == 7.0f,
         "editor metadata should restore editor camera z");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().rotation.w !=
             runtime.gameCameraNode()->getLocalTransform().rotation.w,
         "editor metadata rotation should not fall back to game camera");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().rotation.v.y !=
             runtime.gameCameraNode()->getLocalTransform().rotation.v.y,
         "editor metadata yaw should not match fallback rotation");
  EXPECT(editorCamera->get().fovY == 35.0f,
         "editor metadata should restore editor camera fov");
  EXPECT(editorCamera->get().nearPlane == 0.2f,
         "editor metadata should restore editor camera near plane");
  EXPECT(editorCamera->get().farPlane == 400.0f,
         "editor metadata should restore editor camera far plane");
  EXPECT(editorCamera->get().fovY != gameCamera->get().fovY,
         "editor metadata fov should not be overwritten by fallback");
  EXPECT(editorCamera->get().nearPlane != gameCamera->get().nearPlane,
         "editor metadata near plane should not be overwritten by fallback");
  EXPECT(editorCamera->get().farPlane != gameCamera->get().farPlane,
         "editor metadata far plane should not be overwritten by fallback");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation.x !=
             runtime.gameCameraNode()->getLocalTransform().translation.x,
         "editor metadata should not be overwritten by fallback");
}

void testRuntimeLoadsDefaultDocumentAndBuildsDefaultScene() {
  demo::SceneRuntime runtime;
  runtime.loadDefaultDocument();

  EXPECT(runtime.scene(), "default load should build a scene");
  EXPECT(runtime.documentPath() ==
             resolveRuntimePath("assets/scenes/scene_viewer.scene.yaml"),
         "default load should track the default scene document path");
  EXPECT(runtime.scene()->findByPath("/ground") != nullptr,
         "default scene should include the ground node");
  EXPECT(runtime.scene()->findByPath("/ground/helmet") != nullptr,
         "default scene should include the helmet under the ground root");
  EXPECT(runtime.scene()->findByPath("/dir_light") != nullptr,
         "default scene should include the directional-light node");
  EXPECT(runtime.scene()->findByPath("/editor_cam") ==
             runtime.editorCameraNode().get(),
         "default scene should register the editor camera path");
  EXPECT(runtime.scene()->findByPath("/game_cam") == runtime.gameCameraNode().get(),
         "default scene should register the preview camera path");
}

void testRuntimeSaveAsUpdatesPathAndPersistsCameraState() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_save_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_save_output.yaml");
  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: scene_viewer\n"
                 "gameCamera:\n"
                 "  eye: [0.0, 2.0, 6.0]\n"
                 "  target: [0.0, 0.0, 0.0]\n"
                 "  up: [0.0, 1.0, 0.0]\n"
                 "  fovY: 45.0\n"
                 "  nearPlane: 0.1\n"
                 "  farPlane: 1000.0\n");

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

  gameCamera->get().fovY = 60.0f;
  gameCamera->get().nearPlane = 0.5f;
  gameCamera->get().farPlane = 250.0f;
  gameCamera->get().lookAt(LX_core::Vec3f{7.0f, 8.0f, 9.0f},
                           LX_core::Vec3f{7.0f, 8.0f, 2.0f},
                           LX_core::Vec3f{0.0f, 1.0f, 0.0f});
  gameCamera->get().updateMatrices();

  runtime.saveToDocumentPath(savePath);

  EXPECT(runtime.documentPath() == normalizePath(savePath),
         "save-as should update the current runtime document path");

  const demo::SceneDocument saved = demo::loadSceneDocument(savePath);
  EXPECT(saved.sceneName() == "scene_viewer",
         "save should persist the scene name");
  EXPECT(saved.hasEditorCamera(),
         "save should persist editor camera metadata");
  if (saved.hasEditorCamera()) {
    expectNear(saved.editorCamera().position.x, 4.0f,
               "save should persist editor camera x");
    expectNear(saved.editorCamera().position.y, 5.0f,
               "save should persist editor camera y");
    expectNear(saved.editorCamera().position.z, 6.0f,
               "save should persist editor camera z");
    expectNear(saved.editorCamera().fovY, 35.0f,
               "save should persist editor camera fov");
    expectNear(saved.editorCamera().nearPlane, 0.2f,
               "save should persist editor camera near plane");
    expectNear(saved.editorCamera().farPlane, 400.0f,
               "save should persist editor camera far plane");
  }

  expectNear(saved.gameCamera().eye.x, 7.0f,
             "save should persist game camera eye x");
  expectNear(saved.gameCamera().eye.y, 8.0f,
             "save should persist game camera eye y");
  expectNear(saved.gameCamera().eye.z, 9.0f,
             "save should persist game camera eye z");
  expectNear(saved.gameCamera().target.x, 7.0f,
             "save should persist exact game camera target x");
  expectNear(saved.gameCamera().target.y, 8.0f,
             "save should persist exact game camera target y");
  expectNear(saved.gameCamera().target.z, 2.0f,
             "save should persist exact game camera target z");
  expectNear(saved.gameCamera().fovY, 60.0f,
             "save should persist game camera fov");
  expectNear(saved.gameCamera().nearPlane, 0.5f,
             "save should persist game camera near plane");
  expectNear(saved.gameCamera().farPlane, 250.0f,
             "save should persist game camera far plane");
}

void testRuntimeReloadReplacesSceneAndCameraBindings() {
  const std::filesystem::path firstPath =
      makeTempPath("lx_scene_runtime_reload_first.yaml");
  const std::filesystem::path secondPath =
      makeTempPath("lx_scene_runtime_reload_second.yaml");
  writeSceneFile(firstPath,
                 "scene:\n"
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
                 "    position: [1.0, 2.0, 3.0]\n"
                 "    rotationEulerDeg: [0.0, 45.0, 0.0]\n"
                 "    fovY: 35.0\n"
                 "    nearPlane: 0.2\n"
                 "    farPlane: 400.0\n");
  writeSceneFile(secondPath,
                 "scene:\n"
                 "  name: scene_viewer\n"
                 "gameCamera:\n"
                 "  eye: [3.0, 4.0, 5.0]\n"
                 "  target: [0.0, 0.0, 0.0]\n"
                 "  up: [0.0, 1.0, 0.0]\n"
                 "  fovY: 50.0\n"
                 "  nearPlane: 0.3\n"
                 "  farPlane: 900.0\n"
                 "editor:\n"
                 "  editorCamera:\n"
                 "    position: [9.0, 8.0, 7.0]\n"
                 "    rotationEulerDeg: [0.0, 90.0, 0.0]\n"
                 "    fovY: 25.0\n"
                 "    nearPlane: 0.4\n"
                 "    farPlane: 300.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(firstPath);

  const auto firstScene = runtime.scene();
  const auto firstEditorCamera = runtime.editorCameraNode();
  const auto firstGameCamera = runtime.gameCameraNode();

  runtime.loadFromDocumentPath(secondPath);

  EXPECT(runtime.documentPath() == normalizePath(secondPath),
         "reload should update the tracked document path");
  EXPECT(runtime.scene() != firstScene,
         "reload should replace the active scene object");
  EXPECT(runtime.editorCameraNode() != firstEditorCamera,
         "reload should replace the editor camera node binding");
  EXPECT(runtime.gameCameraNode() != firstGameCamera,
         "reload should replace the preview camera node binding");
  EXPECT(runtime.scene()->findByPath("/editor_cam") ==
             runtime.editorCameraNode().get(),
         "reload should re-register the editor camera path");
  EXPECT(runtime.scene()->findByPath("/game_cam") == runtime.gameCameraNode().get(),
         "reload should re-register the preview camera path");
  expectNear(runtime.editorCameraNode()->getLocalTransform().translation.x, 9.0f,
             "reload should apply the new editor camera x");
  expectNear(runtime.editorCameraNode()->getLocalTransform().translation.y, 8.0f,
             "reload should apply the new editor camera y");
  expectNear(runtime.editorCameraNode()->getLocalTransform().translation.z, 7.0f,
             "reload should apply the new editor camera z");
}

} // namespace

int main() {
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "test_scene_runtime failed to initialize runtime asset root\n";
    return 1;
  }

  testRuntimeFallsBackEditorCameraFromGameCamera();
  testRuntimeRestoresEditorCameraMetadataWhenPresent();
  testRuntimeLoadsDefaultDocumentAndBuildsDefaultScene();
  testRuntimeSaveAsUpdatesPathAndPersistsCameraState();
  testRuntimeReloadReplacesSceneAndCameraBindings();

  if (failures != 0) {
    std::cerr << "test_scene_runtime failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_runtime passed\n";
  return 0;
}
