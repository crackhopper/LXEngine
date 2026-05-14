#include "demos/lxe_editor/scene_runtime.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"
#include "demos/lxe_editor/scene_builder.hpp"
#include "demos/lxe_editor/scene_document.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace LX_demo::lxe_editor {
namespace {

constexpr const char* RuntimeDebugDrawNodePrefix = "debug_draw_";
constexpr const char* LegacyCameraHelperNodeName = "helper_camera";
constexpr const char* LegacyLightHelperNodeName = "helper_light";

struct SceneRuntimeData final {
  std::optional<std::filesystem::path> documentPath;
  std::optional<SceneSourceKind> sourceKind;
  SceneDocument document;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
};

[[nodiscard]] bool isRuntimeDebugDrawNodeName(const std::string& nodeName) {
  return nodeName.rfind(RuntimeDebugDrawNodePrefix, 0) == 0;
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const SceneNodeDocument& nodeDocument) {
  return isRuntimeDebugDrawNodeName(nodeDocument.nodeName);
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const LX_core::SceneNodeSharedPtr& node) {
  return node && isRuntimeDebugDrawNodeName(node->getNodeName());
}

[[nodiscard]] bool isLegacyEditorHelperName(const std::string& name) {
  return name == LegacyCameraHelperNodeName || name == LegacyLightHelperNodeName;
}

[[nodiscard]] bool isLegacyEditorHelperNode(
    const SceneNodeDocument& nodeDocument) {
  return isLegacyEditorHelperName(nodeDocument.nodeName) ||
         isLegacyEditorHelperName(nodeDocument.name);
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const LX_core::SceneNodeSharedPtr& node) {
  return node && (isLegacyEditorHelperName(node->getNodeName()) ||
                  isLegacyEditorHelperName(node->getName()));
}

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

  auto& rootNode = document.mutableRootNode();
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
      .cullingMask = LX_core::Layer_All & ~LX_core::Layer_EditorOverlay &
                     ~Layer_EditorHelper,
  };
  rootNode.children.push_back(std::move(gameCameraNode));

  SceneNodeDocument directionalLightNode;
  directionalLightNode.nodeName = "dir_light_node";
  directionalLightNode.name = "dir_light";
  directionalLightNode.visibilityMask = LX_core::Layer_All;
  directionalLightNode.light = LightNodeState{
      .kind = LightKind::Directional,
      .direction = {-0.3f, -1.0f, -0.5f},
      .color = {1.0f, 0.98f, 0.9f},
      .intensity = 1.0f,
  };
  rootNode.children.push_back(std::move(directionalLightNode));
  return document;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr buildRenderableNodeFromDocument(
    const SceneNodeDocument& nodeDocument) {
  if (!nodeDocument.meshUri.has_value()) {
    return LX_core::SceneNode::create(nodeDocument.nodeName);
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/helmet") {
    auto node = buildHelmetNode(
        resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf"));
    return node;
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/ground_mesh") {
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
  camera.applyProjectionState(state.type, state.fovY, state.aspect,
                              state.nearPlane, state.farPlane, state.left,
                              state.right, state.bottom, state.top);
  camera.setTarget(LX_core::RenderTarget{});
  camera.setCullingMask(state.cullingMask & ~Layer_EditorHelper);
  camera.lookAt(state.eye, state.target, state.up);
  auto local = node.getLocalTransform();
  local.translation = state.eye;
  local.scale = state.type == LX_core::CameraType::Perspective
                    ? node.getLocalTransform().scale
                    : local.scale;
  node.setLocalTransform(local);
}

void configureDirectionalLight(LX_core::DirectionalLight& light,
                               const LightNodeState& state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
}

void configurePointLight(LX_core::PointLight& light,
                         const LightNodeState& state) {
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setRange(state.range);
}

void configureSpotLight(LX_core::SpotLight& light,
                        const LightNodeState& state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setRange(state.range);
  light.setInnerConeDegrees(state.innerConeDegrees);
  light.setOuterConeDegrees(state.outerConeDegrees);
}

void buildSceneNodesRecursive(
    const SceneNodeDocument& nodeDocument, const LX_core::SceneNodeSharedPtr& parent,
    const std::shared_ptr<SceneRuntimeData>& runtime,
    std::unordered_map<std::string, LX_core::SceneNodeSharedPtr>& nodesByPath) {
  if (isRuntimeDebugDrawNode(nodeDocument)) {
    std::cerr << "[lxe_editor] skipping runtime-only scene node: "
              << nodeDocument.nodeName << "\n";
    return;
  }
  if (isLegacyEditorHelperNode(nodeDocument)) {
    std::cerr << "[lxe_editor] skipping legacy editor helper scene node: "
              << nodeDocument.nodeName << "\n";
    return;
  }

  LX_core::SceneNodeSharedPtr node;
  if (nodeDocument.camera.has_value()) {
    node = makeCameraNode(
        nodeDocument.nodeName,
        nodeDocument.name.empty() ? nodeDocument.nodeName : nodeDocument.name,
        nodeDocument.camera->cullingMask);
  } else {
    node = buildRenderableNodeFromDocument(nodeDocument);
  }

  applyNodeIdentityAndTransform(*node, nodeDocument);
  if (parent) {
    node->setParent(parent);
  }

  if (nodeDocument.camera.has_value()) {
    auto& camera = requireCameraComponent(node, nodeDocument.nodeName.c_str()).get();
    applyCameraState(*node, camera, *nodeDocument.camera);
    runtime->scene->addCamera(node);
  } else {
    runtime->scene->addRenderable(node);
  }

  nodesByPath[node->getPath()] = node;

  if (nodeDocument.light.has_value()) {
    switch (nodeDocument.light->kind) {
    case LightKind::Directional: {
      auto light = std::make_shared<LX_core::DirectionalLight>();
      configureDirectionalLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    case LightKind::Point: {
      auto light = std::make_shared<LX_core::PointLight>();
      configurePointLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    case LightKind::Spot: {
      auto light = std::make_shared<LX_core::SpotLight>();
      configureSpotLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    }
  }

  for (const auto& childDocument : nodeDocument.children) {
    buildSceneNodesRecursive(childDocument, node, runtime, nodesByPath);
  }
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
  auto rootNode = runtime->scene->getRootNode();
  applyNodeIdentityAndTransform(*rootNode, document.rootNode());
  for (const auto& childDocument : document.rootNode().children) {
    buildSceneNodesRecursive(childDocument, rootNode, runtime, nodesByPath);
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
  runtime->editorCameraNode->setVisibilityLayerMask(LX_core::Layer_EditorOverlay);
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
    editorCamera.applyProjectionState(
        gameCamera.getProjectionType(), gameCamera.getFovY(),
        gameCamera.getAspect(), gameCamera.getNearPlane(),
        gameCamera.getFarPlane(), gameCamera.getLeft(), gameCamera.getRight(),
        gameCamera.getBottom(), gameCamera.getTop());
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

[[nodiscard]] const SceneNodeDocument*
findDocumentNodeByName(const SceneNodeDocument& node, const std::string& nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (const auto& child : node.children) {
    if (const auto* match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] CameraNodeState
captureCameraState(const LX_core::CameraComponent& camera) {
  return CameraNodeState{
      .eye = camera.getEyePosition(),
      .target = camera.getLookTarget(),
      .up = camera.getUpVector(),
      .type = camera.getProjectionType(),
      .fovY = camera.getFovY(),
      .aspect = camera.getAspect(),
      .nearPlane = camera.getNearPlane(),
      .farPlane = camera.getFarPlane(),
      .left = camera.getLeft(),
      .right = camera.getRight(),
      .bottom = camera.getBottom(),
      .top = camera.getTop(),
      .cullingMask = camera.getCullingMask(),
  };
}

[[nodiscard]] LightNodeState
captureDirectionalLightState(const LX_core::DirectionalLight& light) {
  return LightNodeState{
      .kind = LightKind::Directional,
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
  };
}

[[nodiscard]] LightNodeState capturePointLightState(const LX_core::PointLight& light) {
  return LightNodeState{
      .kind = LightKind::Point,
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .range = light.getRange(),
  };
}

[[nodiscard]] LightNodeState captureSpotLightState(const LX_core::SpotLight& light) {
  return LightNodeState{
      .kind = LightKind::Spot,
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .range = light.getRange(),
      .innerConeDegrees = light.getInnerConeDegrees(),
      .outerConeDegrees = light.getOuterConeDegrees(),
  };
}

[[nodiscard]] SceneDocument
captureSceneDocument(const std::shared_ptr<SceneRuntimeData>& runtime) {
  SceneDocument document;
  document.setSceneName(runtime->scene ? runtime->scene->getSceneName() : "Scene");
  document.setGameplayCameraPath(runtime->gameCameraNode
                                     ? runtime->gameCameraNode->getPath()
                                     : "/game_cam");

  auto captureNode = [&](const auto& self,
                         const LX_core::SceneNodeSharedPtr& node)
      -> SceneNodeDocument {
    SceneNodeDocument entry;
    entry.nodeName = node->getNodeName();
    entry.name = node->getName();
    entry.transform = node->getLocalTransform();
    entry.visibilityMask = node->getVisibilityLayerMask();

    if (const auto* existing =
            findDocumentNodeByName(runtime->document.rootNode(),
                                   node->getNodeName())) {
      entry.visibilityMask = existing->visibilityMask;
      entry.meshUri = existing->meshUri;
      entry.materialUri = existing->materialUri;
    } else if (node->getName() == "helmet") {
      entry.meshUri = "builtin://lxe_editor/helmet";
    } else if (node->getName() == "ground") {
      entry.meshUri = "builtin://lxe_editor/ground_mesh";
      entry.materialUri = "builtin://lxe_editor/ground_material";
    }

    if (const auto camera = node->getComponent<LX_core::CameraComponent>();
        camera.has_value()) {
      entry.camera = captureCameraState(camera->get());
    }

    if (const auto light = runtime->scene->getLight(*node)) {
      if (const auto directional =
              std::dynamic_pointer_cast<LX_core::DirectionalLight>(light)) {
        entry.light = captureDirectionalLightState(*directional);
      } else if (const auto point =
                     std::dynamic_pointer_cast<LX_core::PointLight>(light)) {
        entry.light = capturePointLightState(*point);
      } else if (const auto spot =
                     std::dynamic_pointer_cast<LX_core::SpotLight>(light)) {
        entry.light = captureSpotLightState(*spot);
      }
    }

    for (const auto& child : node->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) ||
          isLegacyEditorHelperNode(child)) {
        continue;
      }
      entry.children.push_back(self(self, child));
    }

    return entry;
  };

  auto& rootEntry = document.mutableRootNode();
  if (runtime->scene && runtime->scene->getRootNode()) {
    const auto& rootNode = runtime->scene->getRootNode();
    rootEntry.nodeName = rootNode->getNodeName();
    rootEntry.name = rootNode->getName();
    rootEntry.parentPath.clear();
    rootEntry.transform = rootNode->getLocalTransform();
    rootEntry.visibilityMask = rootNode->getVisibilityLayerMask();
    rootEntry.meshUri.reset();
    rootEntry.materialUri.reset();
    rootEntry.camera.reset();
    rootEntry.light.reset();
    rootEntry.children.clear();

    for (const auto& child : rootNode->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) ||
          isLegacyEditorHelperNode(child)) {
        continue;
      }
      rootEntry.children.push_back(captureNode(captureNode, child));
    }
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

} // namespace LX_demo::lxe_editor
