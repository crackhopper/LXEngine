#include "demos/scene_viewer/scene_runtime.hpp"

#include "core/utils/filesystem_tools.hpp"
#include "core/scene/components/camera_component.hpp"
#include "demos/scene_viewer/editor_camera_state.hpp"
#include "demos/scene_viewer/scene_document.hpp"
#include "demos/scene_viewer/scene_builder.hpp"
#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace LX_demo::scene_viewer {
namespace {

struct SceneRuntimeData final {
  std::filesystem::path documentPath;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
};

[[nodiscard]] LX_core::Vec3f loadVec3(const YAML::Node& node,
                                      const char* fieldName) {
  if (!node || !node.IsSequence() || node.size() != 3) {
    throw std::runtime_error(std::string("expected vec3 sequence for ") +
                             fieldName);
  }

  return LX_core::Vec3f{node[0].as<float>(), node[1].as<float>(),
                        node[2].as<float>()};
}

[[nodiscard]] GameCameraState loadGameCamera(const YAML::Node& node) {
  GameCameraState state;
  if (!node) {
    return state;
  }

  state.eye = loadVec3(node["eye"], "gameCamera.eye");
  state.target = loadVec3(node["target"], "gameCamera.target");
  state.up = loadVec3(node["up"], "gameCamera.up");
  state.fovY = node["fovY"].as<float>();
  state.nearPlane = node["nearPlane"].as<float>();
  state.farPlane = node["farPlane"].as<float>();
  return state;
}

[[nodiscard]] std::optional<EditorCameraState>
loadEditorCamera(const YAML::Node& node) {
  if (!node) {
    return std::nullopt;
  }

  return EditorCameraState{
      .position = loadVec3(node["position"], "editor.editorCamera.position"),
      .rotationEulerDeg =
          loadVec3(node["rotationEulerDeg"],
                   "editor.editorCamera.rotationEulerDeg"),
      .fovY = node["fovY"].as<float>(),
      .nearPlane = node["nearPlane"].as<float>(),
      .farPlane = node["farPlane"].as<float>(),
  };
}

[[nodiscard]] SceneDocument
loadSceneDocumentForRuntime(const std::filesystem::path& path) {
  const YAML::Node root = YAML::LoadFile(path.string());
  const YAML::Node editorNode = root["editor"];
  if (editorNode && editorNode["editorCamera"]) {
    return loadSceneDocument(path);
  }

  SceneDocument document;
  if (const YAML::Node sceneNode = root["scene"]; sceneNode) {
    if (const YAML::Node nameNode = sceneNode["name"]; nameNode) {
      document.setSceneName(nameNode.as<std::string>());
    }
  }

  document.mutableGameCamera() = loadGameCamera(root["gameCamera"]);

  if (editorNode) {
    if (const auto editorCamera = loadEditorCamera(editorNode["editorCamera"]);
        editorCamera.has_value()) {
      document.setEditorCamera(*editorCamera);
    }
  }

  return document;
}

[[nodiscard]] std::filesystem::path
normalizeDocumentPath(const std::filesystem::path& path) {
  if (path.empty()) {
    throw std::runtime_error("scene document path is empty");
  }

  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr& node,
                       const char* nodeLabel) {
  if (!node) {
    throw std::runtime_error(std::string("missing scene node: ") + nodeLabel);
  }

  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error(std::string("missing camera component on ") +
                             nodeLabel);
  }

  return camera->get();
}

[[nodiscard]] LX_core::SceneNodeSharedPtr
makeCameraNode(const std::string& nodeName, const std::string& displayName,
               LX_core::VisibilityLayerMask cullingMask) {
  auto node = LX_core::SceneNode::create(nodeName);
  node->setName(displayName);
  const auto camera = node->addComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error("failed to create camera component for " +
                             nodeName);
  }
  camera->get().setTarget(LX_core::RenderTarget{});
  camera->get().setCullingMask(cullingMask);
  return node;
}

void configureDefaultDirectionalLight(LX_core::Scene& scene) {
  const auto directionalLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
      scene.getLights().front());
  if (!directionalLight || !directionalLight->ubo) {
    throw std::runtime_error("scene runtime expected a seeded directional light");
  }

  directionalLight->ubo->param.dir = LX_core::Vec4f{-0.3f, -1.0f, -0.5f, 0.0f};
  directionalLight->ubo->param.color =
      LX_core::Vec4f{1.0f, 0.98f, 0.9f, 1.0f};
  directionalLight->ubo->setDirty();
}

[[nodiscard]] LX_core::SceneSharedPtr
buildDefaultScene(const SceneDocument& document) {
  const std::filesystem::path gltfPath =
      resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf");
  auto helmet = buildHelmetNode(gltfPath);
  auto ground = buildGroundNode();
  helmet->setName("helmet");
  ground->setName("ground");
  helmet->setParent(ground);

  auto scene = LX_core::Scene::create(document.sceneName(), helmet);
  scene->addRenderable(ground);

  auto lightNode = LX_core::SceneNode::create("dir_light_node");
  lightNode->setName("dir_light");
  scene->addRenderable(lightNode);
  configureDefaultDirectionalLight(*scene);
  return scene;
}

void restoreGameCamera(const GameCameraState& state, LX_core::SceneNode& node,
                       LX_core::CameraComponent& camera) {
  camera.fovY = state.fovY;
  camera.nearPlane = state.nearPlane;
  camera.farPlane = state.farPlane;
  camera.lookAt(state.eye, state.target, state.up);
  camera.updateMatrices();
}

void copyCameraState(const LX_core::SceneNode& sourceNode,
                     const LX_core::CameraComponent& sourceCamera,
                     LX_core::SceneNode& destNode,
                     LX_core::CameraComponent& destCamera) {
  destCamera.lookAt(sourceCamera.getEyePosition(), sourceCamera.getLookTarget(),
                    sourceCamera.getUpVector());
  auto destTransform = destNode.getLocalTransform();
  destTransform.scale = sourceNode.getLocalTransform().scale;
  destNode.setLocalTransform(destTransform.normalized());
  destCamera.fovY = sourceCamera.fovY;
  destCamera.nearPlane = sourceCamera.nearPlane;
  destCamera.farPlane = sourceCamera.farPlane;
  destCamera.updateMatrices();
}

[[nodiscard]] GameCameraState
captureGameCameraState(const LX_core::CameraComponent& camera) {
  const LX_core::Vec3f eye = camera.getEyePosition();
  return GameCameraState{
      .eye = eye,
      .target = camera.getLookTarget(),
      .up = camera.getUpVector(),
      .fovY = camera.fovY,
      .nearPlane = camera.nearPlane,
      .farPlane = camera.farPlane,
  };
}

[[nodiscard]] SceneDocument captureSceneDocument(const SceneRuntimeData& runtime) {
  SceneDocument document;
  if (runtime.scene) {
    document.setSceneName(runtime.scene->getSceneName());
  }

  auto& gameCamera =
      requireCameraComponent(runtime.gameCameraNode, "game_camera").get();
  document.mutableGameCamera() = captureGameCameraState(gameCamera);

  auto& editorCamera =
      requireCameraComponent(runtime.editorCameraNode, "editor_camera").get();
  document.setEditorCamera(
      EditorCameraState::captureFrom(*runtime.editorCameraNode, editorCamera));
  return document;
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
requireRuntimeData(const std::shared_ptr<void>& impl) {
  if (!impl) {
    throw std::runtime_error("scene runtime is not loaded");
  }
  return std::static_pointer_cast<SceneRuntimeData>(impl);
}

} // namespace

void SceneRuntime::loadDefaultDocument() {
  loadFromDocumentPath(resolveRuntimePath("assets/scenes/scene_viewer.scene.yaml"));
}

void SceneRuntime::loadFromDocumentPath(const std::filesystem::path& path) {
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  const SceneDocument document = loadSceneDocumentForRuntime(normalizedPath);

  auto runtime = std::make_shared<SceneRuntimeData>();
  runtime->documentPath = normalizedPath;
  runtime->scene = buildDefaultScene(document);
  runtime->editorCameraNode =
      makeCameraNode("editor_camera", "editor_cam", LX_core::Layer_All);
  runtime->gameCameraNode = makeCameraNode(
      "game_camera", "game_cam",
      LX_core::Layer_All & ~LX_core::Layer_EditorOverlay);

  auto& gameCamera =
      requireCameraComponent(runtime->gameCameraNode, "game_camera").get();
  restoreGameCamera(document.gameCamera(), *runtime->gameCameraNode, gameCamera);

  auto& editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  if (document.hasEditorCamera()) {
    document.editorCamera().applyTo(*runtime->editorCameraNode, editorCamera);
  } else {
    copyCameraState(*runtime->gameCameraNode, gameCamera,
                    *runtime->editorCameraNode, editorCamera);
  }

  runtime->scene->addCamera(runtime->editorCameraNode);
  runtime->scene->addCamera(runtime->gameCameraNode);

  m_impl = std::move(runtime);
}

void SceneRuntime::saveToCurrentDocumentPath() {
  const auto runtime = requireRuntimeData(m_impl);
  saveSceneDocument(runtime->documentPath, captureSceneDocument(*runtime));
}

void SceneRuntime::saveToDocumentPath(const std::filesystem::path& path) {
  const auto runtime = requireRuntimeData(m_impl);
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  saveSceneDocument(normalizedPath, captureSceneDocument(*runtime));
  runtime->documentPath = normalizedPath;
}

std::filesystem::path SceneRuntime::documentPath() const {
  if (!m_impl) {
    return {};
  }
  return std::static_pointer_cast<const SceneRuntimeData>(m_impl)->documentPath;
}

LX_core::SceneSharedPtr SceneRuntime::scene() const {
  if (!m_impl) {
    return nullptr;
  }
  return std::static_pointer_cast<const SceneRuntimeData>(m_impl)->scene;
}

LX_core::SceneNodeSharedPtr SceneRuntime::editorCameraNode() const {
  if (!m_impl) {
    return nullptr;
  }
  return std::static_pointer_cast<const SceneRuntimeData>(m_impl)
      ->editorCameraNode;
}

LX_core::SceneNodeSharedPtr SceneRuntime::gameCameraNode() const {
  if (!m_impl) {
    return nullptr;
  }
  return std::static_pointer_cast<const SceneRuntimeData>(m_impl)
      ->gameCameraNode;
}

} // namespace LX_demo::scene_viewer
