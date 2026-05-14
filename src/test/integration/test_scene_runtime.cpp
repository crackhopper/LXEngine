#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"
#include "demos/lxe_editor/scene_builder.hpp"
#include "demos/lxe_editor/scene_document.hpp"
#include "demos/lxe_editor/scene_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cstring>
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

[[nodiscard]] std::filesystem::path makeTempPath(const char *filename) {
  return std::filesystem::temp_directory_path() / filename;
}

[[nodiscard]] std::string readFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void writeSceneFile(const std::filesystem::path &path,
                    const std::string &body) {
  std::ofstream out(path);
  out << body;
}

void expectNear(const float lhs, const float rhs, const char *msg,
                const float epsilon = 0.001f) {
  if (std::abs(lhs - rhs) > epsilon) {
    std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg
              << " (" << lhs << " vs " << rhs << ")\n";
    ++failures;
  }
}

[[nodiscard]] std::optional<LX_core::Vec3f>
readNodeBaseColor(const LX_core::SceneNodeSharedPtr& node) {
  if (!node) {
    return std::nullopt;
  }
  const auto materialComponent =
      node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value() ||
      !materialComponent->get().getMaterialInstance()) {
    return std::nullopt;
  }
  const auto material = materialComponent->get().getMaterialInstance();
  const auto layout =
      material->getParameterBufferLayout(LX_core::StringID("MaterialUBO"));
  if (!layout.has_value()) {
    return std::nullopt;
  }
  const auto memberIt = std::find_if(
      layout->get().members.begin(), layout->get().members.end(),
      [](const auto& member) { return member.name == "baseColor"; });
  if (memberIt == layout->get().members.end()) {
    return std::nullopt;
  }
  const auto& bytes =
      material->getParameterBufferBytes(LX_core::StringID("MaterialUBO"));
  LX_core::Vec3f color{};
  std::memcpy(&color, bytes.data() + memberIt->offset, sizeof(float) * 3);
  return color;
}

void testRuntimeCreatesEmptyScene() {
  demo::SceneRuntime runtime;
  runtime.createEmptyScene();

  EXPECT(runtime.scene(), "empty runtime should create a scene");
  EXPECT(runtime.scene()->findByPath("/") ==
             runtime.scene()->getRootNode().get(),
         "empty runtime should expose the explicit scene root at slash");
  EXPECT(!runtime.documentPath().has_value(),
         "empty runtime should not have a document path");
  EXPECT(runtime.gameCameraNode(),
         "empty runtime should have a gameplay camera");
  EXPECT(runtime.editorCameraNode(),
         "empty runtime should have an editor camera");
  EXPECT(runtime.gameCameraNode()->getParent() ==
             runtime.scene()->getRootNode(),
         "gameplay camera should attach under the scene root");
  EXPECT(runtime.editorCameraNode()->getParent() ==
             runtime.scene()->getRootNode(),
         "editor camera should attach under the scene root");
  EXPECT(runtime.editorCameraNode()->getVisibilityLayerMask() ==
             LX_core::Layer_EditorOverlay,
         "editor camera should be an editor-only node excluded from scene "
         "picking");
  EXPECT(runtime.scene()->findByPath("/helmet") == nullptr,
         "empty runtime should not create a helmet node");
  EXPECT(runtime.scene()->findByPath("/ground") == nullptr,
         "empty runtime should not create a ground node");
  EXPECT(runtime.scene()->findByPath("/dir_light") != nullptr,
         "empty runtime should create a default directional light node");
  EXPECT(runtime.scene()->getDirectionalLight(
             *runtime.scene()->findByPath("/dir_light")) != nullptr,
         "empty runtime should attach a directional light to the default light "
         "node");
  EXPECT(runtime.scene()->findByPath("/game_cam/helper_camera") == nullptr,
         "empty runtime should not create gameplay-camera helper child nodes");
  EXPECT(!runtime.gameCameraNode()
              ->getComponent<LX_core::MeshComponent>()
              .has_value(),
         "gameplay camera should not carry a helper mesh");
  EXPECT(
      runtime.scene()->getPickBounds(*runtime.gameCameraNode()).isValid(),
      "gameplay camera should expose debug pick bounds without a helper mesh");
  const LX_core::BoundingBox gameCameraBounds =
      runtime.scene()->getPickBounds(*runtime.gameCameraNode());
  EXPECT(gameCameraBounds.min.y > 1.7f && gameCameraBounds.max.y < 2.3f &&
             gameCameraBounds.min.z > 5.7f && gameCameraBounds.max.z < 6.3f,
         "gameplay camera pick bounds should be a small box around the eye");
  EXPECT(runtime.scene()->findByPath("/dir_light/helper_light") == nullptr,
         "empty runtime should not create a directional-light helper child");
  EXPECT(runtime.scene()->getRenderables().size() == 3,
         "empty runtime should contain gameplay camera, editor camera, and "
         "default light");
}

void testRuntimeCreatesEditorOnlyHelpersForEditableSceneNodes() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_helpers.yaml");
  writeSceneFile(path, "scene:\n"
                       "  name: helper_scene\n"
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
                       "      cullingMask: 4294967295\n"
                       "  - nodeName: dir_light_node\n"
                       "    name: dir_light\n"
                       "    transform:\n"
                       "      translation: [1.0, 2.0, 3.0]\n"
                       "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                       "      scale: [1.0, 1.0, 1.0]\n"
                       "    visibilityMask: 4294967295\n"
                       "    directionalLight:\n"
                       "      direction: [-0.3, -1.0, -0.5]\n"
                       "      color: [1.0, 0.98, 0.9]\n"
                       "      intensity: 1.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.scene()->findByPath("/game_cam/helper_camera") == nullptr,
         "game camera should not get an editor-only helper child");
  EXPECT(!runtime.gameCameraNode()
              ->getComponent<LX_core::MeshComponent>()
              .has_value(),
         "game camera should not carry a helper mesh");
  EXPECT(runtime.scene()->getPickBounds(*runtime.gameCameraNode()).isValid(),
         "game camera should expose debug pick bounds without a helper mesh");
  EXPECT(runtime.scene()->findByPath("/dir_light/helper_light") == nullptr,
         "directional light should not get an editor-only helper child");
}

void testRuntimeDoesNotCreateCameraHelperVisualAtStaleTransform() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_camera_eye_helper.yaml");
  writeSceneFile(path, "scene:\n"
                       "  name: camera_eye_helper_scene\n"
                       "  gameplayCameraPath: /game_cam\n"
                       "nodes:\n"
                       "  - nodeName: game_camera\n"
                       "    name: game_cam\n"
                       "    transform:\n"
                       "      translation: [0.0, 0.0, 0.0]\n"
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
                       "  - nodeName: dir_light_node\n"
                       "    name: dir_light\n"
                       "    transform:\n"
                       "      translation: [0.0, 0.0, 0.0]\n"
                       "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                       "      scale: [1.0, 1.0, 1.0]\n"
                       "    visibilityMask: 4294967295\n"
                       "    directionalLight:\n"
                       "      direction: [-0.3, -1.0, -0.5]\n"
                       "      color: [1.0, 0.98, 0.9]\n"
                       "      intensity: 1.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  const LX_core::Vec3f translation =
      runtime.gameCameraNode()->getLocalTransform().translation;
  expectNear(translation.x, 0.0f,
             "camera node transform should use camera eye x");
  expectNear(translation.y, 2.0f,
             "camera node transform should use camera eye y");
  expectNear(translation.z, 6.0f,
             "camera node transform should use camera eye z");
  EXPECT(!runtime.gameCameraNode()
              ->getComponent<LX_core::MeshComponent>()
              .has_value(),
         "camera should not create a stale helper visual mesh");
}

void testRuntimeLoadsTypedPointAndSpotLights() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_typed_lights.yaml");
  writeSceneFile(path,
                 "scene:\n"
                 "  name: typed_light_scene\n"
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
                 "      cullingMask: 4294967295\n"
                 "  - nodeName: point_node\n"
                 "    name: point\n"
                 "    transform:\n"
                 "      translation: [1.0, 2.0, 3.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    light:\n"
                 "      kind: Point\n"
                 "      color: [0.8, 0.7, 0.6]\n"
                 "      intensity: 2.0\n"
                 "      range: 6.0\n"
                 "  - nodeName: spot_node\n"
                 "    name: spot\n"
                 "    transform:\n"
                 "      translation: [0.0, 3.0, 1.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    light:\n"
                 "      kind: Spot\n"
                 "      direction: [0.0, -0.5, -1.0]\n"
                 "      color: [0.7, 0.8, 1.0]\n"
                 "      intensity: 3.0\n"
                 "      range: 8.0\n"
                 "      innerConeDegrees: 20.0\n"
                 "      outerConeDegrees: 35.0\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  auto* pointNode = runtime.scene()->findByPath("/point");
  auto* spotNode = runtime.scene()->findByPath("/spot");
  EXPECT(pointNode != nullptr && spotNode != nullptr,
         "typed light nodes should load");
  if (pointNode != nullptr && spotNode != nullptr) {
    EXPECT(runtime.scene()->getPointLight(*pointNode) != nullptr,
           "point light runtime instance should attach");
    const auto spot = runtime.scene()->getSpotLight(*spotNode);
    EXPECT(spot != nullptr, "spot light runtime instance should attach");
    EXPECT(spot != nullptr && spot->getOuterConeDegrees() == 35.0f,
           "spot light cone should load");
  }

  const auto resources = runtime.scene()->getSceneLevelResources(
      LX_core::Pass_Forward, LX_core::RenderTarget{});
  LX_core::SceneLightsDataSharedPtr sceneLights;
  for (const auto& resource : resources) {
    if (resource && resource->getBindingName() ==
                        LX_core::StringID("SceneLightsUBO")) {
      sceneLights = std::dynamic_pointer_cast<LX_core::SceneLightsData>(resource);
    }
  }
  EXPECT(sceneLights != nullptr, "scene resources should expose SceneLightsUBO");
  if (sceneLights != nullptr) {
    EXPECT(sceneLights->param.counts.x == 0 && sceneLights->param.counts.y == 1 &&
               sceneLights->param.counts.z == 1,
           "SceneLightsUBO should count typed point and spot lights");
  }
}

void testRuntimeSkipsLegacyEditorHelperNodesOnLoad() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_legacy_helpers.yaml");
  writeSceneFile(path, "scene:\n"
                       "  name: legacy_helper_scene\n"
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
                       "      cullingMask: 4294967295\n"
                       "  - nodeName: dir_light_node\n"
                       "    name: dir_light\n"
                       "    transform:\n"
                       "      translation: [2.0, 0.0, 0.0]\n"
                       "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                       "      scale: [1.0, 1.0, 1.0]\n"
                       "    visibilityMask: 4294967295\n"
                       "    directionalLight:\n"
                       "      direction: [-0.3, -1.0, -0.5]\n"
                       "      color: [1.0, 0.98, 0.9]\n"
                       "      intensity: 1.0\n"
                       "    children:\n"
                       "      - nodeName: helper_light\n"
                       "        name: helper_light\n"
                       "        transform:\n"
                       "          translation: [-2.0, 0.0, 0.0]\n"
                       "          rotation: [1.0, 0.0, 0.0, 0.0]\n"
                       "          scale: [1.0, 1.0, 1.0]\n"
                       "        visibilityMask: 1073741824\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.scene()->findByPath("/dir_light") != nullptr,
         "legacy scene should still load the light owner");
  EXPECT(runtime.scene()->findByPath("/dir_light/helper_light") == nullptr,
         "legacy light helper child should be dropped on load");
}

void testRuntimeLoadsFullSceneDocument() {
  const std::filesystem::path path = makeTempPath("lx_scene_runtime_full.yaml");
  writeSceneFile(path, "scene:\n"
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
  EXPECT(runtime.documentPath().has_value(),
         "loaded runtime should track path");
  EXPECT(runtime.scene()->findByPath("/") ==
             runtime.scene()->getRootNode().get(),
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
  writeSceneFile(path, "scene:\n"
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

  EXPECT(runtime.scene()->findByPath("/") ==
             runtime.scene()->getRootNode().get(),
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
  writeSceneFile(inputPath, "scene:\n"
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
  }
      .applyTo(*runtime.editorCameraNode(), editorCamera->get());

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
  EXPECT(saved.sceneName() == "sample_scene", "save should persist scene name");
  EXPECT(saved.gameplayCameraPath() == "/game_cam",
         "save should persist gameplay camera path");
  EXPECT(saved.hasEditorCamera(), "save should persist editor camera metadata");
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

void testRuntimeSavePreservesDuplicatedDocumentPayloads() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_duplicate_payload_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_duplicate_payload_output.yaml");
  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: duplicate_payload_scene\n"
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
                 "      cullingMask: 4294967295\n"
                 "  - nodeName: cube_node\n"
                 "    name: cube\n"
                 "    transform:\n"
                 "      translation: [1.0, 0.0, 0.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    visibilityMask: 7\n"
                 "    mesh:\n"
                 "      uri: test://mesh/cube\n"
                 "    material:\n"
                 "      uri: test://material/red\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(inputPath);
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::registerBuiltinCommands(bus, editorState, *runtime.scene());

  const LX_core::CommandResult copy = bus.dispatch("copy /cube");
  EXPECT(copy.ok, "copy primitive payload node should succeed");
  const LX_core::CommandResult paste = bus.dispatch("paste_as_sibling /cube");
  EXPECT(paste.ok, "paste primitive payload node should succeed");

  runtime.saveToDocumentPath(savePath);
  const demo::SceneDocument saved = demo::loadSceneDocument(savePath);

  const demo::SceneNodeDocument* copied = nullptr;
  for (const auto& child : saved.rootNode().children) {
    if (child.name == "cube.copy") {
      copied = &child;
      break;
    }
  }
  EXPECT(copied != nullptr, "saved document should include duplicated node");
  if (copied != nullptr) {
    EXPECT(copied->meshUri.has_value() &&
               *copied->meshUri == "test://mesh/cube",
           "duplicated primitive should preserve meshUri");
    EXPECT(copied->materialUri.has_value() &&
               *copied->materialUri == "test://material/red",
           "duplicated primitive should preserve materialUri");
    expectNear(copied->transform.translation.x, 1.5f,
               "duplicated primitive should save default local +X offset");
  }
}

void testRuntimeSaveSyncsRenamedGameplayCameraPath() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_rename_gameplay_camera_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_rename_gameplay_camera_output.yaml");
  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: rename_gameplay_camera_scene\n"
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
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::registerBuiltinCommands(bus, editorState, *runtime.scene());

  const LX_core::CommandResult rename =
      bus.dispatch("set /game_cam.name gameplay_main");
  EXPECT(rename.ok, "rename gameplay camera should succeed");

  runtime.saveToDocumentPath(savePath);
  const demo::SceneDocument saved = demo::loadSceneDocument(savePath);
  EXPECT(saved.gameplayCameraPath() == "/gameplay_main",
         "save should sync gameplayCameraPath after gameplay camera rename");
}

void testRuntimeSkipsLegacyDebugDrawNodesOnLoad() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_legacy_debug_draw.yaml");
  writeSceneFile(path, "scene:\n"
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
                       "      cullingMask: 4294967295\n"
                       "  - nodeName: debug_draw_2147483648\n"
                       "    name: debug_draw_2147483648\n"
                       "    transform:\n"
                       "      translation: [0.0, 0.0, 0.0]\n"
                       "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                       "      scale: [1.0, 1.0, 1.0]\n"
                       "    visibilityMask: 2147483648\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);

  EXPECT(runtime.scene()->findByPath("/debug_draw_2147483648") == nullptr,
         "legacy runtime debug draw nodes should not load as editable scene "
         "nodes");

  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(runtime.scene());
  LX_core::DebugDraw::beginFrame();
  LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  bool debugDrawSucceeded = false;
  try {
    debugDrawSucceeded = LX_core::DebugDraw::endFrame();
  } catch (const std::exception &ex) {
    std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__
              << " DebugDraw should not collide with loaded scene nodes: "
              << ex.what() << "\n";
    ++failures;
  }
  EXPECT(debugDrawSucceeded,
         "debug draw should create its runtime bucket after loading scene");
  LX_core::DebugDraw::reset();
}

void testRuntimeSaveOmitsDebugDrawRuntimeNodes() {
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_save_debug_draw.yaml");

  demo::SceneRuntime runtime;
  runtime.createEmptyScene();

  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(runtime.scene());
  LX_core::DebugDraw::beginFrame();
  LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  LX_core::DebugDraw::endFrame();

  runtime.saveToDocumentPath(savePath);
  LX_core::DebugDraw::reset();

  const std::string savedText = readFile(savePath);
  EXPECT(savedText.find("debug_draw_") == std::string::npos,
         "save should omit DebugDraw runtime nodes");
}

void testRuntimeSaveOmitsLegacyEditorHelperNodes() {
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_save_legacy_helpers.yaml");

  demo::SceneRuntime runtime;
  runtime.createEmptyScene();

  auto staleHelper = LX_core::SceneNode::create("helper_light");
  staleHelper->setName("helper_light");
  staleHelper->setParent(
      runtime.scene()->findByPath("/dir_light")->shared_from_this());
  runtime.scene()->addRenderable(staleHelper);

  runtime.saveToDocumentPath(savePath);

  const std::string savedText = readFile(savePath);
  EXPECT(savedText.find("helper_light") == std::string::npos,
         "save should omit legacy light helper nodes");
}

void testRuntimeMaterialUriAndBaseColorOverridesRoundTrip() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_material_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_material_save.yaml");

  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: material_scene\n"
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
                 "      cullingMask: 4294967295\n"
                 "  - nodeName: ground\n"
                 "    name: ground_a\n"
                 "    transform:\n"
                 "      translation: [0.0, 0.0, 0.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    visibilityMask: 4294967295\n"
                 "    mesh:\n"
                 "      uri: builtin://lxe_editor/ground_mesh\n"
                 "    material:\n"
                 "      uri: assets/materials/blinnphong_lit.material\n"
                 "  - nodeName: helmet\n"
                 "    name: helmet\n"
                 "    transform:\n"
                 "      translation: [2.0, 0.0, 0.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    visibilityMask: 4294967295\n"
                 "    mesh:\n"
                 "      uri: builtin://lxe_editor/helmet\n"
                 "    material:\n"
                 "      uri: assets/materials/blinnphong_lit.material\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(inputPath);

  const auto setColor =
      runtime.setNodeMaterialBaseColor("/ground_a", {0.2f, 0.3f, 0.4f});
  EXPECT(setColor.ok, "setting node baseColor override should succeed");

  const auto groundA =
      runtime.scene()->findByPath("/ground_a")->shared_from_this();
  const auto groundB =
      runtime.scene()->findByPath("/helmet")->shared_from_this();
  const auto colorA = readNodeBaseColor(groundA);
  const auto colorB = readNodeBaseColor(groundB);
  EXPECT(colorA.has_value() && colorB.has_value(),
         "both ground nodes should expose MaterialUBO.baseColor");
  if (colorA.has_value() && colorB.has_value()) {
    expectNear(colorA->x, 0.2f,
               "node override should update only selected node red");
    expectNear(colorA->y, 0.3f,
               "node override should update only selected node green");
    expectNear(colorB->x, 0.8f,
         "node override should not mutate sibling material red");
  }

  const auto apply = runtime.applyMaterialOverride("/ground_a", "baseColor");
  EXPECT(apply.ok, "applying baseColor override should succeed");
  runtime.saveToDocumentPath(savePath);

  const demo::SceneDocument saved = demo::loadSceneDocument(savePath);
  EXPECT(saved.rootNode().children.size() == 3,
         "saved material scene should keep all root children");
  const auto& savedGroundA = saved.rootNode().children[1];
  const auto& savedGroundB = saved.rootNode().children[2];
  EXPECT(savedGroundA.nodeMaterialOverrides.baseColor.has_value(),
         "node-level override should persist on selected node");
  EXPECT(savedGroundA.materialOverrides.baseColor.has_value(),
         "applied material override should persist on selected node material config");
  EXPECT(savedGroundB.materialOverrides.baseColor.has_value(),
         "applied material override should persist on same-URI sibling material config");
}

void testGenericNodeMaterialParameterOverrideRoundTrips() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_generic_material_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_generic_material_output.yaml");
  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: rtr_material_scene\n"
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
                 "      cullingMask: 4294967295\n"
                 "  - nodeName: primitive_cube_node\n"
                 "    name: cube_a\n"
                 "    transform:\n"
                 "      translation: [0.0, 0.0, 0.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    visibilityMask: 4294967295\n"
                 "    mesh:\n"
                 "      uri: builtin://lxe_editor/primitives/cube\n"
                 "    material:\n"
                 "      uri: assets/materials/rtr_experiment_template.material\n"
                 "  - nodeName: primitive_sphere_node\n"
                 "    name: sphere_b\n"
                 "    transform:\n"
                 "      translation: [2.0, 0.0, 0.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    visibilityMask: 4294967295\n"
                 "    mesh:\n"
                 "      uri: builtin://lxe_editor/primitives/sphere\n"
                 "    material:\n"
                 "      uri: assets/materials/rtr_experiment_template.material\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(inputPath);

  LX_core::MaterialParameterValue value;
  value.type = LX_core::MaterialParameterValueType::Float;
  value.floatValue = 0.75f;
  const auto set = runtime.setNodeMaterialParameter(
      "/cube_a", "MaterialUBO", "mixAmount", value);
  EXPECT(set.ok, "setting generic node material parameter should succeed");

  const auto cubeValue = runtime.nodeMaterialParameterForNode(
      "/cube_a", "MaterialUBO", "mixAmount");
  const auto sphereValue = runtime.nodeMaterialParameterForNode(
      "/sphere_b", "MaterialUBO", "mixAmount");
  EXPECT(cubeValue.has_value(),
         "selected node should expose updated generic float parameter");
  if (cubeValue.has_value()) {
    expectNear(cubeValue->floatValue, 0.75f,
               "selected node generic float override should update runtime");
  }
  EXPECT(sphereValue.has_value(),
         "sibling node should expose generic float parameter");
  if (sphereValue.has_value()) {
    expectNear(sphereValue->floatValue, 0.35f,
               "generic float override should not mutate sibling runtime");
  }

  runtime.saveToDocumentPath(savePath);
  const demo::SceneDocument saved = demo::loadSceneDocument(savePath);
  const auto& savedCube = saved.rootNode().children[1];
  const auto& savedSphere = saved.rootNode().children[2];
  EXPECT(savedCube.nodeMaterialOverrides.parameters.count(
             "MaterialUBO.mixAmount") == 1,
         "generic node material override should persist to scene document");
  EXPECT(savedSphere.nodeMaterialOverrides.parameters.empty(),
         "sibling should not receive generic material override");
}

void testGroundMeshWindingMatchesUpwardNormal() {
  const auto ground = demo::buildGroundNode();
  const auto meshComponent = ground->getComponent<LX_core::MeshComponent>();
  EXPECT(meshComponent.has_value(), "ground node should have mesh component");
  if (!meshComponent.has_value()) {
    return;
  }

  const auto &mesh = meshComponent->get().getMesh();
  const auto *vertexBuffer =
      dynamic_cast<LX_core::VertexBuffer<LX_core::VertexPosNormalUvBone> *>(
          mesh->vertexBuffer.get());
  EXPECT(vertexBuffer != nullptr,
         "ground mesh should use VertexPosNormalUvBone vertices");
  EXPECT(mesh->indexBuffer != nullptr,
         "ground mesh should have an index buffer");
  if (!mesh->indexBuffer) {
    return;
  }
  EXPECT(mesh->indexBuffer->getTopology() ==
             LX_core::PrimitiveTopology::TriangleList,
         "ground should be a triangle-list mesh");
  EXPECT(mesh->indexBuffer->indexCount() == 6,
         "ground should use two triangles");
  const auto *indices =
      static_cast<const u32 *>(mesh->indexBuffer->getRawData());
  EXPECT(indices[0] == 0 && indices[1] == 2 && indices[2] == 1 &&
             indices[3] == 0 && indices[4] == 3 && indices[5] == 2,
         "ground winding should match the upward normal convention");
}

void testBuiltinPrimitiveScenePayloadRoundTrips() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_builtin_primitive_input.yaml");
  const std::filesystem::path savePath =
      makeTempPath("lx_scene_runtime_builtin_primitive_output.yaml");
  writeSceneFile(inputPath,
                 "scene:\n"
                 "  name: primitive_scene\n"
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
                 "      cullingMask: 4294967295\n"
                 "  - nodeName: primitive_cube_node\n"
                 "    name: Cube\n"
                 "    transform:\n"
                 "      translation: [1.0, 0.5, 2.0]\n"
                 "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                 "      scale: [1.0, 1.0, 1.0]\n"
                 "    visibilityMask: 4294967295\n"
                 "    mesh:\n"
                 "      uri: builtin://lxe_editor/primitives/cube\n"
                 "    material:\n"
                 "      uri: assets/materials/blinnphong_lit.material\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(inputPath);

  auto *cube = runtime.scene()->findByPath("/Cube");
  EXPECT(cube != nullptr, "builtin primitive should load as a scene node");
  EXPECT(cube != nullptr &&
             cube->getComponent<LX_core::MeshComponent>().has_value(),
         "builtin primitive should create a mesh component");
  runtime.saveToDocumentPath(savePath);
  const std::string savedText = readFile(savePath);
  EXPECT(savedText.find("builtin://lxe_editor/primitives/cube") !=
             std::string::npos,
         "save should preserve builtin primitive mesh URI");
  EXPECT(savedText.find("assets/materials/blinnphong_lit.material") !=
             std::string::npos,
         "save should preserve builtin primitive material URI");
}

} // namespace

int main() {
  testRuntimeCreatesEmptyScene();
  testRuntimeCreatesEditorOnlyHelpersForEditableSceneNodes();
  testRuntimeDoesNotCreateCameraHelperVisualAtStaleTransform();
  testRuntimeLoadsTypedPointAndSpotLights();
  testRuntimeSkipsLegacyEditorHelperNodesOnLoad();
  testRuntimeLoadsFullSceneDocument();
  testRuntimeLoadsLegacyFlatSceneDocumentWithExplicitRootNormalization();
  testRuntimeSaveRoundTripsExpandedSceneDocument();
  testRuntimeSavePreservesDuplicatedDocumentPayloads();
  testRuntimeSaveSyncsRenamedGameplayCameraPath();
  testRuntimeSkipsLegacyDebugDrawNodesOnLoad();
  testRuntimeSaveOmitsDebugDrawRuntimeNodes();
  testRuntimeSaveOmitsLegacyEditorHelperNodes();
  testRuntimeMaterialUriAndBaseColorOverridesRoundTrip();
  testGenericNodeMaterialParameterOverrideRoundTrips();
  testGroundMeshWindingMatchesUpwardNormal();
  testBuiltinPrimitiveScenePayloadRoundTrips();

  if (failures != 0) {
    std::cerr << "test_scene_runtime failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_runtime passed\n";
  return 0;
}
