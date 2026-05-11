#include "demos/scene_viewer/scene_runtime.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/scene_viewer/editor_camera_state.hpp"
#include "demos/scene_viewer/scene_builder.hpp"
#include "demos/scene_viewer/scene_document.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace LX_demo::scene_viewer {
namespace {

struct SceneRuntimeData final {
  std::optional<std::filesystem::path> documentPath;
  std::optional<SceneSourceKind> sourceKind;
  SceneDocument document;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
  std::unordered_map<const LX_core::SceneNode*, LX_core::DirectionalLightSharedPtr>
      directionalLightsByNode;
};

[[nodiscard]] std::filesystem::path normalizeDocumentPath(
    const std::filesystem::path& path) {
  if (path.empty()) {
    throw std::runtime_error("scene document path is empty");
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

[[nodiscard]] LX_core::SceneNodeSharedPtr makeCameraNode(
    const std::string& nodeName, const std::string& displayName,
    const LX_core::VisibilityLayerMask cullingMask) {
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

[[nodiscard]] std::string
cameraPathToDisplayName(const std::string& path, const std::string& fallback) {
  if (path.empty() || path == "/") {
    return fallback;
  }
  const auto slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  return name.empty() ? fallback : name;
}

[[nodiscard]] SceneDocument makeEmptySceneDocument() {
  SceneDocument document;
  document.setSceneName("Scene");
  document.setGameplayCameraPath("/game_cam");

  auto& nodes = document.mutableNodes();
  SceneNodeDocument gameCameraNode;
  gameCameraNode.nodeName = "game_camera";
  gameCameraNode.name = "game_cam";
  gameCameraNode.camera = CameraNodeState{
      .eye = {0.0f, 2.0f, 6.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .type = LX_core::CameraType::Perspective,
      .fovY = 45.0f,
      .aspect = 16.0f / 9.0f,
      .nearPlane = 0.1f,
      .farPlane = 1000.0f,
      .left = -1.0f,
      .right = 1.0f,
      .bottom = -1.0f,
      .top = 1.0f,
      .cullingMask = LX_core::Layer_All & ~LX_core::Layer_EditorOverlay,
  };
  nodes.push_back(std::move(gameCameraNode));
  return document;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr buildRenderableNodeFromDocument(
    const SceneNodeDocument& nodeDocument) {
  if (!nodeDocument.meshUri.has_value()) {
    return LX_core::SceneNode::create(nodeDocument.nodeName);
  }

  if (*nodeDocument.meshUri == "builtin://scene_viewer/helmet") {
    auto node = buildHelmetNode(
        resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf"));
    return node;
  }

  if (*nodeDocument.meshUri == "builtin://scene_viewer/ground_mesh") {
    return buildGroundNode();
  }

  // Placeholder for future generic mesh import support.
  return LX_core::SceneNode::create(nodeDocument.nodeName);
}

void applyNodeIdentityAndTransform(LX_core::SceneNode& node,
                                   const SceneNodeDocument& documentNode) {
  node.setName(documentNode.name);
  node.setLocalTransform(documentNode.transform);
  node.setVisibilityLayerMask(documentNode.visibilityMask);
}

void applyCameraState(LX_core::SceneNode& node, LX_core::CameraComponent& camera,
                      const CameraNodeState& state) {
  camera.type = state.type;
  camera.fovY = state.fovY;
  camera.aspect = state.aspect;
  camera.nearPlane = state.nearPlane;
  camera.farPlane = state.farPlane;
  camera.left = state.left;
  camera.right = state.right;
  camera.bottom = state.bottom;
  camera.top = state.top;
  camera.setTarget(LX_core::RenderTarget{});
  camera.setCullingMask(state.cullingMask);
  camera.lookAt(state.eye, state.target, state.up);
  camera.updateMatrices();
  auto local = node.getLocalTransform();
  local.scale = state.type == LX_core::CameraType::Perspective
                    ? node.getLocalTransform().scale
                    : local.scale;
  node.setLocalTransform(local);
}

void configureDirectionalLight(LX_core::DirectionalLight& light,
                               const DirectionalLightNodeState& state) {
  light.ubo->param.dir =
      LX_core::Vec4f{state.direction.x, state.direction.y, state.direction.z, 0.0f};
  light.ubo->param.color =
      LX_core::Vec4f{state.color.x, state.color.y, state.color.z, state.intensity};
  light.ubo->setDirty();
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
buildRuntimeFromDocument(const SceneDocument& document,
                         const std::optional<std::filesystem::path>& path,
                         const std::optional<SceneSourceKind> sourceKind) {
  auto runtime = std::make_shared<SceneRuntimeData>();
  runtime->documentPath = path;
  runtime->sourceKind = sourceKind;
  runtime->document = document;
  runtime->scene = LX_core::Scene::create(document.sceneName(), nullptr);

  while (!runtime->scene->getLights().empty()) {
    runtime->scene->removeLight(runtime->scene->getLights().front());
  }

  std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> nodesByPath;
  std::vector<std::pair<LX_core::SceneNodeSharedPtr, std::string>> parentLinks;
  std::vector<std::pair<LX_core::SceneNodeSharedPtr, DirectionalLightNodeState>>
      lightNodes;

  for (const auto& nodeDocument : document.nodes()) {
    LX_core::SceneNodeSharedPtr node;
    if (nodeDocument.camera.has_value()) {
      node = makeCameraNode(
          nodeDocument.nodeName,
          nodeDocument.name.empty() ? nodeDocument.nodeName : nodeDocument.name,
          nodeDocument.camera->cullingMask);
      auto& camera = requireCameraComponent(node, nodeDocument.nodeName.c_str()).get();
      applyCameraState(*node, camera, *nodeDocument.camera);
      runtime->scene->addCamera(node);
    } else {
      node = buildRenderableNodeFromDocument(nodeDocument);
      applyNodeIdentityAndTransform(*node, nodeDocument);
      runtime->scene->addRenderable(node);
    }

    applyNodeIdentityAndTransform(*node, nodeDocument);
    const std::string nodePath =
        nodeDocument.parentPath.empty()
            ? ("/" + nodeDocument.name)
            : (nodeDocument.parentPath + "/" + nodeDocument.name);
    nodesByPath[nodePath] = node;

    if (!nodeDocument.parentPath.empty()) {
      parentLinks.emplace_back(node, nodeDocument.parentPath);
    }
    if (nodeDocument.directionalLight.has_value()) {
      lightNodes.emplace_back(node, *nodeDocument.directionalLight);
    }
  }

  for (const auto& [node, parentPath] : parentLinks) {
    const auto it = nodesByPath.find(parentPath);
    if (it == nodesByPath.end()) {
      throw std::runtime_error("scene document parent path not found: " + parentPath);
    }
    node->setParent(it->second);
  }

  for (const auto& [node, lightState] : lightNodes) {
    auto light = std::make_shared<LX_core::DirectionalLight>();
    configureDirectionalLight(*light, lightState);
    runtime->scene->addLight(light);
    runtime->directionalLightsByNode[node.get()] = light;
  }

  const std::string gameplayPath = document.gameplayCameraPath();
  const auto gameplayNodeIt = nodesByPath.find(gameplayPath);
  if (gameplayNodeIt == nodesByPath.end()) {
    throw std::runtime_error("gameplay camera path not found in scene document: " +
                             gameplayPath);
  }
  runtime->gameCameraNode = gameplayNodeIt->second;

  runtime->editorCameraNode =
      makeCameraNode("editor_camera", "editor_cam", LX_core::Layer_All);
  auto& editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  auto& gameCamera =
      requireCameraComponent(runtime->gameCameraNode, "game_camera").get();
  if (document.hasEditorCamera()) {
    document.editorCamera().applyTo(*runtime->editorCameraNode, editorCamera);
  } else {
    editorCamera.lookAt(gameCamera.getEyePosition(), gameCamera.getLookTarget(),
                        gameCamera.getUpVector());
    runtime->editorCameraNode->setLocalTransform(
        runtime->gameCameraNode->getLocalTransform());
    editorCamera.fovY = gameCamera.fovY;
    editorCamera.aspect = gameCamera.aspect;
    editorCamera.nearPlane = gameCamera.nearPlane;
    editorCamera.farPlane = gameCamera.farPlane;
    editorCamera.left = gameCamera.left;
    editorCamera.right = gameCamera.right;
    editorCamera.bottom = gameCamera.bottom;
    editorCamera.top = gameCamera.top;
    editorCamera.updateMatrices();
  }
  runtime->scene->addCamera(runtime->editorCameraNode);

  return runtime;
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
requireRuntimeData(const std::shared_ptr<void>& impl) {
  if (!impl) {
    throw std::runtime_error("scene runtime is not loaded");
  }
  return std::static_pointer_cast<SceneRuntimeData>(impl);
}

[[nodiscard]] std::vector<LX_core::SceneNodeSharedPtr>
collectSerializableNodes(const std::shared_ptr<SceneRuntimeData>& runtime) {
  std::vector<LX_core::SceneNodeSharedPtr> nodes;
  std::unordered_set<const LX_core::SceneNode*> seen;
  for (const auto& renderable : runtime->scene->getRenderables()) {
    const auto node = std::dynamic_pointer_cast<LX_core::SceneNode>(renderable);
    if (node == runtime->editorCameraNode) {
      continue;
    }
    if (node && seen.insert(node.get()).second) {
      nodes.push_back(node);
    }
  }
  for (const auto& camera : runtime->scene->getCameras()) {
    if (camera == runtime->editorCameraNode) {
      continue;
    }
    if (camera && seen.insert(camera.get()).second) {
      nodes.push_back(camera);
    }
  }
  return nodes;
}

[[nodiscard]] std::optional<SceneNodeDocument>
findDocumentNodeByName(const SceneDocument& document, const std::string& nodeName) {
  for (const auto& node : document.nodes()) {
    if (node.nodeName == nodeName) {
      return node;
    }
  }
  return std::nullopt;
}

[[nodiscard]] CameraNodeState
captureCameraState(const LX_core::CameraComponent& camera) {
  return CameraNodeState{
      .eye = camera.getEyePosition(),
      .target = camera.getLookTarget(),
      .up = camera.getUpVector(),
      .type = camera.type,
      .fovY = camera.fovY,
      .aspect = camera.aspect,
      .nearPlane = camera.nearPlane,
      .farPlane = camera.farPlane,
      .left = camera.left,
      .right = camera.right,
      .bottom = camera.bottom,
      .top = camera.top,
      .cullingMask = camera.getCullingMask(),
  };
}

[[nodiscard]] DirectionalLightNodeState
captureDirectionalLightState(const LX_core::DirectionalLight& light) {
  return DirectionalLightNodeState{
      .direction = {light.ubo->param.dir.x, light.ubo->param.dir.y, light.ubo->param.dir.z},
      .color = {light.ubo->param.color.x, light.ubo->param.color.y, light.ubo->param.color.z},
      .intensity = light.ubo->param.color.w,
  };
}

[[nodiscard]] SceneDocument
captureSceneDocument(const std::shared_ptr<SceneRuntimeData>& runtime) {
  SceneDocument document;
  document.setSceneName(runtime->scene ? runtime->scene->getSceneName() : "Scene");
  document.setGameplayCameraPath(runtime->gameCameraNode
                                     ? runtime->gameCameraNode->getPath()
                                     : "/game_cam");

  auto& nodes = document.mutableNodes();
  for (const auto& node : collectSerializableNodes(runtime)) {
    SceneNodeDocument entry;
    entry.nodeName = node->getNodeName();
    entry.name = node->getName();
    if (const auto parent = node->getParent()) {
      entry.parentPath = parent->getPath();
    }
    entry.transform = node->getLocalTransform();
    entry.visibilityMask = node->getVisibilityLayerMask();

    if (const auto existing = findDocumentNodeByName(runtime->document, node->getNodeName());
        existing.has_value()) {
      entry.meshUri = existing->meshUri;
      entry.materialUri = existing->materialUri;
    } else if (node->getName() == "helmet") {
      entry.meshUri = "builtin://scene_viewer/helmet";
    } else if (node->getName() == "ground") {
      entry.meshUri = "builtin://scene_viewer/ground_mesh";
      entry.materialUri = "builtin://scene_viewer/ground_material";
    }

    if (const auto camera = node->getComponent<LX_core::CameraComponent>();
        camera.has_value()) {
      entry.camera = captureCameraState(camera->get());
    }

    const auto lightIt = runtime->directionalLightsByNode.find(node.get());
    if (lightIt != runtime->directionalLightsByNode.end() && lightIt->second) {
      entry.directionalLight =
          captureDirectionalLightState(*lightIt->second);
    }

    nodes.push_back(std::move(entry));
  }

  auto& editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  document.setEditorCamera(
      EditorCameraState::captureFrom(*runtime->editorCameraNode, editorCamera));
  return document;
}

} // namespace

void SceneRuntime::createEmptyScene() {
  m_impl = buildRuntimeFromDocument(makeEmptySceneDocument(), std::nullopt,
                                    std::nullopt);
}

void SceneRuntime::loadFromDocumentPath(
    const std::filesystem::path& path,
    const std::optional<SceneSourceKind> sourceKind) {
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  const SceneDocument document = loadSceneDocument(normalizedPath);
  m_impl = buildRuntimeFromDocument(document, normalizedPath, sourceKind);
}

void SceneRuntime::saveToCurrentDocumentPath() {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->documentPath.has_value()) {
    throw std::runtime_error("scene runtime has no current document path");
  }
  saveToDocumentPath(*runtime->documentPath);
}

void SceneRuntime::saveToDocumentPath(const std::filesystem::path& path) {
  const auto runtime = requireRuntimeData(m_impl);
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  SceneDocument document = captureSceneDocument(runtime);
  saveSceneDocument(normalizedPath, document);
  runtime->document = std::move(document);
  runtime->documentPath = normalizedPath;
}

std::optional<std::filesystem::path> SceneRuntime::documentPath() const {
  const auto runtime = requireRuntimeData(m_impl);
  return runtime->documentPath;
}

std::optional<SceneSourceKind> SceneRuntime::sourceKind() const {
  const auto runtime = requireRuntimeData(m_impl);
  return runtime->sourceKind;
}

LX_core::SceneSharedPtr SceneRuntime::scene() const {
  return requireRuntimeData(m_impl)->scene;
}

LX_core::SceneNodeSharedPtr SceneRuntime::editorCameraNode() const {
  return requireRuntimeData(m_impl)->editorCameraNode;
}

LX_core::SceneNodeSharedPtr SceneRuntime::gameCameraNode() const {
  return requireRuntimeData(m_impl)->gameCameraNode;
}

} // namespace LX_demo::scene_viewer
