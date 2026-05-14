#include "demos/lxe_editor/scene_runtime.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"
#include "demos/lxe_editor/scene_builder.hpp"
#include "demos/lxe_editor/scene_document.hpp"
#include "infra/material_loader/generic_material_loader.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace LX_demo::lxe_editor {
namespace {

constexpr const char *RuntimeDebugDrawNodePrefix = "debug_draw_";
constexpr const char *LegacyCameraHelperNodeName = "helper_camera";
constexpr const char *LegacyLightHelperNodeName = "helper_light";
constexpr const char *BuiltinPrimitivePrefix =
    "builtin://lxe_editor/primitives/";
constexpr const char *BuiltinPrimitiveMaterial =
    "assets/materials/blinnphong_lit.material";
constexpr const char *kDefaultGroundMaterial =
    "assets/materials/blinnphong_lit.material";
constexpr const char *kDefaultHelmetMaterial =
    "assets/materials/blinnphong_textured.material";

struct SceneRuntimeData final {
  std::optional<std::filesystem::path> documentPath;
  std::optional<SceneSourceKind> sourceKind;
  SceneDocument document;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
};

[[nodiscard]] bool isRuntimeDebugDrawNodeName(const std::string &nodeName) {
  return nodeName.rfind(RuntimeDebugDrawNodePrefix, 0) == 0;
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const SceneNodeDocument &nodeDocument) {
  return isRuntimeDebugDrawNodeName(nodeDocument.nodeName);
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const LX_core::SceneNodeSharedPtr &node) {
  return node && isRuntimeDebugDrawNodeName(node->getNodeName());
}

[[nodiscard]] bool isLegacyEditorHelperName(const std::string &name) {
  return name == LegacyCameraHelperNodeName ||
         name == LegacyLightHelperNodeName;
}

[[nodiscard]] bool isBuiltinPrimitiveMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinPrimitivePrefix, 0) == 0;
}

[[nodiscard]] std::optional<std::string>
primitiveUriFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "primitive_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  const std::string shape = nodeName.substr(begin, suffixPos - begin);
  if (shape != "cube" && shape != "sphere" && shape != "plane" &&
      shape != "cylinder" && shape != "cone") {
    return std::nullopt;
  }
  return std::string(BuiltinPrimitivePrefix) + shape;
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const SceneNodeDocument &nodeDocument) {
  return isLegacyEditorHelperName(nodeDocument.nodeName) ||
         isLegacyEditorHelperName(nodeDocument.name);
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const LX_core::SceneNodeSharedPtr &node) {
  return node && (isLegacyEditorHelperName(node->getNodeName()) ||
                  isLegacyEditorHelperName(node->getName()));
}

[[nodiscard]] std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string makeVec3Json(const LX_core::Vec3f &value) {
  std::ostringstream oss;
  oss << "{\"x\":" << value.x << ",\"y\":" << value.y << ",\"z\":"
      << value.z << "}";
  return oss.str();
}

[[nodiscard]] LX_core::CommandResult makeCommandError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}, {}};
}

[[nodiscard]] LX_core::CommandResult makeCommandOk(std::string message,
                                                   std::string structured) {
  return LX_core::CommandResult{true, std::move(message),
                                std::move(structured), {}};
}

[[nodiscard]] const std::vector<std::string> &materialPresetUris() {
  static const std::vector<std::string> kPresets = {
      "assets/materials/blinnphong_lit.material",
      "assets/materials/blinnphong_default.material",
      "assets/materials/blinnphong_textured.material",
      "assets/materials/pbr_gold.material",
  };
  return kPresets;
}

[[nodiscard]] bool isAllowedMaterialPreset(const std::string &uri) {
  const auto &presets = materialPresetUris();
  return std::find(presets.begin(), presets.end(), uri) != presets.end();
}

[[nodiscard]] std::string normalizeMaterialUri(const SceneNodeDocument &node) {
  if (node.materialUri.has_value()) {
    if (*node.materialUri == "builtin://lxe_editor/ground_material") {
      return kDefaultGroundMaterial;
    }
    return *node.materialUri;
  }
  if (node.meshUri == "builtin://lxe_editor/helmet") {
    return kDefaultHelmetMaterial;
  }
  if (node.meshUri == "builtin://lxe_editor/ground_mesh") {
    return kDefaultGroundMaterial;
  }
  return kDefaultGroundMaterial;
}

[[nodiscard]] bool documentNodeHasMaterialSurface(
    const SceneNodeDocument &node) {
  return node.meshUri.has_value() || node.materialUri.has_value();
}

[[nodiscard]] bool materialHasBaseColor(
    const LX_core::MaterialInstanceSharedPtr &material) {
  if (!material) {
    return false;
  }
  const auto layout = material->getParameterBufferLayout(
      LX_core::StringID("MaterialUBO"));
  if (!layout.has_value()) {
    return false;
  }
  const auto &members = layout->get().members;
  return std::any_of(members.begin(), members.end(), [](const auto &member) {
    return member.name == "baseColor" &&
           member.type == LX_core::ShaderPropertyType::Vec3;
  });
}

void applyBaseColorIfSupported(
    const LX_core::MaterialInstanceSharedPtr &material,
    const std::optional<LX_core::Vec3f> &color) {
  if (!material || !color.has_value() || !materialHasBaseColor(material)) {
    return;
  }
  material->setParameter(LX_core::StringID("MaterialUBO"),
                         LX_core::StringID("baseColor"), *color);
  material->syncGpuData();
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadMaterialForSceneNode(const std::string &uri,
                         const MaterialOverrideState &materialOverrides,
                         const MaterialOverrideState &nodeOverrides) {
  auto material = LX_infra::loadGenericMaterial(uri);
  if (!material) {
    throw std::runtime_error("failed to load material: " + uri);
  }
  applyBaseColorIfSupported(material, materialOverrides.baseColor);
  applyBaseColorIfSupported(material, nodeOverrides.baseColor);
  return material;
}

[[nodiscard]] std::filesystem::path normalizeDocumentPath(
    const std::filesystem::path &path) {
  if (path.empty()) {
    throw std::runtime_error("scene document path is empty");
  }
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr &node,
                       const char *nodeLabel) {
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
makeCameraNode(const std::string &nodeName, const std::string &displayName,
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

[[nodiscard]] std::string cameraPathToDisplayName(const std::string &path,
                                                  const std::string &fallback) {
  if (path.empty() || path == "/") {
    return fallback;
  }
  const auto slash = path.find_last_of('/');
  const std::string name =
      slash == std::string::npos ? path : path.substr(slash + 1);
  return name.empty() ? fallback : name;
}

[[nodiscard]] SceneDocument makeEmptySceneDocument() {
  SceneDocument document;
  document.setSceneName("Scene");
  document.setGameplayCameraPath("/game_cam");

  auto &rootNode = document.mutableRootNode();
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
  directionalLightNode.directionalLight = DirectionalLightNodeState{
      .direction = {-0.3f, -1.0f, -0.5f},
      .color = {1.0f, 0.98f, 0.9f},
      .intensity = 1.0f,
  };
  rootNode.children.push_back(std::move(directionalLightNode));
  return document;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr
buildRenderableNodeFromDocument(const SceneNodeDocument &nodeDocument) {
  if (!nodeDocument.meshUri.has_value()) {
    return LX_core::SceneNode::create(nodeDocument.nodeName);
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/helmet") {
    auto node = buildHelmetNode(
        resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf"));
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          nodeDocument.nodeMaterialOverrides.baseColor.has_value() ||
          nodeDocument.materialOverrides.baseColor.has_value()) {
        materialComponent->get().setMaterialInstance(loadMaterialForSceneNode(
            uri, nodeDocument.materialOverrides,
            nodeDocument.nodeMaterialOverrides));
      } else {
        applyBaseColorIfSupported(
            materialComponent->get().getMaterialInstance(),
            nodeDocument.materialOverrides.baseColor);
        applyBaseColorIfSupported(
            materialComponent->get().getMaterialInstance(),
            nodeDocument.nodeMaterialOverrides.baseColor);
      }
    }
    return node;
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/ground_mesh") {
    auto node = buildGroundNode();
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          nodeDocument.nodeMaterialOverrides.baseColor.has_value() ||
          nodeDocument.materialOverrides.baseColor.has_value()) {
        materialComponent->get().setMaterialInstance(loadMaterialForSceneNode(
            uri, nodeDocument.materialOverrides,
            nodeDocument.nodeMaterialOverrides));
      }
    }
    return node;
  }

  if (isBuiltinPrimitiveMeshUri(*nodeDocument.meshUri)) {
    return buildBuiltinPrimitiveNode(*nodeDocument.meshUri,
                                     nodeDocument.nodeName);
  }

  // Placeholder for future generic mesh import support.
  return LX_core::SceneNode::create(nodeDocument.nodeName);
}

void applyNodeIdentityAndTransform(LX_core::SceneNode &node,
                                   const SceneNodeDocument &documentNode) {
  node.setName(documentNode.name);
  node.setLocalTransform(documentNode.transform);
  node.setVisibilityLayerMask(documentNode.visibilityMask);
}

void applyCameraState(LX_core::SceneNode &node,
                      LX_core::CameraComponent &camera,
                      const CameraNodeState &state) {
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

void configureDirectionalLight(LX_core::DirectionalLight &light,
                               const DirectionalLightNodeState &state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
}

void buildSceneNodesRecursive(
    const SceneNodeDocument &nodeDocument,
    const LX_core::SceneNodeSharedPtr &parent,
    const std::shared_ptr<SceneRuntimeData> &runtime,
    std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> &nodesByPath) {
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
    node = makeCameraNode(nodeDocument.nodeName,
                          nodeDocument.name.empty() ? nodeDocument.nodeName
                                                    : nodeDocument.name,
                          nodeDocument.camera->cullingMask);
  } else {
    node = buildRenderableNodeFromDocument(nodeDocument);
  }

  applyNodeIdentityAndTransform(*node, nodeDocument);
  if (parent) {
    node->setParent(parent);
  }

  if (nodeDocument.camera.has_value()) {
    auto &camera =
        requireCameraComponent(node, nodeDocument.nodeName.c_str()).get();
    applyCameraState(*node, camera, *nodeDocument.camera);
    runtime->scene->addCamera(node);
  } else {
    runtime->scene->addRenderable(node);
  }

  nodesByPath[node->getPath()] = node;

  if (nodeDocument.directionalLight.has_value()) {
    auto light = std::make_shared<LX_core::DirectionalLight>();
    configureDirectionalLight(*light, *nodeDocument.directionalLight);
    runtime->scene->attachLight(node, light);
  }

  for (const auto &childDocument : nodeDocument.children) {
    buildSceneNodesRecursive(childDocument, node, runtime, nodesByPath);
  }
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
buildRuntimeFromDocument(const SceneDocument &document,
                         const std::optional<std::filesystem::path> &path,
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
  for (const auto &childDocument : document.rootNode().children) {
    buildSceneNodesRecursive(childDocument, rootNode, runtime, nodesByPath);
  }

  const std::string gameplayPath = document.gameplayCameraPath();
  const auto gameplayNodeIt = nodesByPath.find(gameplayPath);
  if (gameplayNodeIt == nodesByPath.end()) {
    throw std::runtime_error(
        "gameplay camera path not found in scene document: " + gameplayPath);
  }
  runtime->gameCameraNode = gameplayNodeIt->second;

  runtime->editorCameraNode =
      makeCameraNode("editor_camera", "editor_cam", LX_core::Layer_All);
  runtime->editorCameraNode->setVisibilityLayerMask(
      LX_core::Layer_EditorOverlay);
  auto &editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  auto &gameCamera =
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
requireRuntimeData(const std::shared_ptr<void> &impl) {
  if (!impl) {
    throw std::runtime_error("scene runtime is not loaded");
  }
  return std::static_pointer_cast<SceneRuntimeData>(impl);
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeByName(const SceneNodeDocument &node,
                       const std::string &nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (const auto &child : node.children) {
    if (const auto *match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] SceneNodeDocument*
findDocumentNodeByName(SceneNodeDocument& node, const std::string& nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (auto& child : node.children) {
    if (auto* match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] SceneNodeDocument*
findDocumentNodeForRuntimePath(SceneRuntimeData& runtime,
                               const std::string& path) {
  if (!runtime.scene) {
    return nullptr;
  }
  LX_core::SceneNode* node = runtime.scene->findByPath(path);
  if (!node) {
    return nullptr;
  }
  return findDocumentNodeByName(runtime.document.mutableRootNode(),
                                node->getNodeName());
}

[[nodiscard]] const SceneNodeDocument*
findDocumentNodeForRuntimePath(const SceneRuntimeData& runtime,
                               const std::string& path) {
  if (!runtime.scene) {
    return nullptr;
  }
  LX_core::SceneNode* node = runtime.scene->findByPath(path);
  if (!node) {
    return nullptr;
  }
  return findDocumentNodeByName(runtime.document.rootNode(),
                                node->getNodeName());
}

void forEachDocumentNode(SceneNodeDocument& node,
                         const std::function<void(SceneNodeDocument&)>& fn) {
  fn(node);
  for (auto& child : node.children) {
    forEachDocumentNode(child, fn);
  }
}

void forEachRuntimeNode(const LX_core::SceneNodeSharedPtr& node,
                        const std::function<void(LX_core::SceneNode&)>& fn) {
  if (!node) {
    return;
  }
  fn(*node);
  for (const auto& child : node->getChildren()) {
    forEachRuntimeNode(child, fn);
  }
}

[[nodiscard]] CameraNodeState
captureCameraState(const LX_core::CameraComponent &camera) {
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

[[nodiscard]] DirectionalLightNodeState
captureDirectionalLightState(const LX_core::DirectionalLight &light) {
  return DirectionalLightNodeState{
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
  };
}

[[nodiscard]] SceneDocument
captureSceneDocument(const std::shared_ptr<SceneRuntimeData> &runtime) {
  SceneDocument document;
  document.setSceneName(runtime->scene ? runtime->scene->getSceneName()
                                       : "Scene");
  document.setGameplayCameraPath(runtime->gameCameraNode
                                     ? runtime->gameCameraNode->getPath()
                                     : "/game_cam");

  auto captureNode =
      [&](const auto &self,
          const LX_core::SceneNodeSharedPtr &node) -> SceneNodeDocument {
    SceneNodeDocument entry;
    entry.nodeName = node->getNodeName();
    entry.name = node->getName();
    entry.transform = node->getLocalTransform();
    entry.visibilityMask = node->getVisibilityLayerMask();

    if (const auto *existing = findDocumentNodeByName(
            runtime->document.rootNode(), node->getNodeName())) {
      entry.visibilityMask = existing->visibilityMask;
      entry.meshUri = existing->meshUri;
      entry.materialUri = existing->materialUri;
      entry.nodeMaterialOverrides = existing->nodeMaterialOverrides;
      entry.materialOverrides = existing->materialOverrides;
    } else if (node->getName() == "helmet") {
      entry.meshUri = "builtin://lxe_editor/helmet";
      entry.materialUri = kDefaultHelmetMaterial;
    } else if (node->getName() == "ground") {
      entry.meshUri = "builtin://lxe_editor/ground_mesh";
      entry.materialUri = kDefaultGroundMaterial;
    } else if (const auto primitiveUri =
                   primitiveUriFromNodeName(node->getNodeName())) {
      entry.meshUri = *primitiveUri;
      entry.materialUri = BuiltinPrimitiveMaterial;
    }

    if (const auto camera = node->getComponent<LX_core::CameraComponent>();
        camera.has_value()) {
      entry.camera = captureCameraState(camera->get());
    }

    if (const auto light = runtime->scene->getDirectionalLight(*node)) {
      entry.directionalLight = captureDirectionalLightState(*light);
    }

    for (const auto &child : node->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) || isLegacyEditorHelperNode(child)) {
        continue;
      }
      entry.children.push_back(self(self, child));
    }

    return entry;
  };

  auto &rootEntry = document.mutableRootNode();
  if (runtime->scene && runtime->scene->getRootNode()) {
    const auto &rootNode = runtime->scene->getRootNode();
    rootEntry.nodeName = rootNode->getNodeName();
    rootEntry.name = rootNode->getName();
    rootEntry.parentPath.clear();
    rootEntry.transform = rootNode->getLocalTransform();
    rootEntry.visibilityMask = rootNode->getVisibilityLayerMask();
    rootEntry.meshUri.reset();
    rootEntry.materialUri.reset();
    rootEntry.nodeMaterialOverrides = MaterialOverrideState{};
    rootEntry.materialOverrides = MaterialOverrideState{};
    rootEntry.camera.reset();
    rootEntry.directionalLight.reset();
    rootEntry.children.clear();

    for (const auto &child : rootNode->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) || isLegacyEditorHelperNode(child)) {
        continue;
      }
      rootEntry.children.push_back(captureNode(captureNode, child));
    }
  }

  auto &editorCamera =
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
    const std::filesystem::path &path,
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

void SceneRuntime::saveToDocumentPath(const std::filesystem::path &path) {
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

std::optional<std::string>
SceneRuntime::materialUriForNode(const std::string& path) const {
  const auto runtime = requireRuntimeData(m_impl);
  const auto* documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return std::nullopt;
  }
  if (!documentNode->meshUri.has_value() && !documentNode->materialUri.has_value()) {
    return std::nullopt;
  }
  return normalizeMaterialUri(*documentNode);
}

std::optional<LX_core::Vec3f>
SceneRuntime::nodeMaterialBaseColorForNode(const std::string& path) const {
  const auto runtime = requireRuntimeData(m_impl);
  const auto* documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return std::nullopt;
  }
  if (documentNode->nodeMaterialOverrides.baseColor.has_value()) {
    return documentNode->nodeMaterialOverrides.baseColor;
  }
  if (documentNode->materialOverrides.baseColor.has_value()) {
    return documentNode->materialOverrides.baseColor;
  }
  return LX_core::Vec3f{0.8f, 0.8f, 0.8f};
}

bool SceneRuntime::nodeMaterialBaseColorEditable(const std::string& path) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return false;
  }
  LX_core::SceneNode* node = runtime->scene->findByPath(path);
  if (!node) {
    return false;
  }
  const auto materialComponent =
      node->getComponent<LX_core::MaterialComponent>();
  return materialComponent.has_value() &&
         materialHasBaseColor(materialComponent->get().getMaterialInstance());
}

std::vector<std::string> SceneRuntime::materialPresets() const {
  return materialPresetUris();
}

LX_core::CommandResult
SceneRuntime::setNodeMaterialUri(const std::string& path,
                                 const std::string& uri) {
  if (!isAllowedMaterialPreset(uri)) {
    return makeCommandError("unsupported material preset: " + uri);
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto* documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode* node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }

  try {
    auto material = loadMaterialForSceneNode(
        uri, documentNode->materialOverrides, documentNode->nodeMaterialOverrides);
    auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
    if (materialComponent.has_value()) {
      materialComponent->get().setMaterialInstance(std::move(material));
    } else {
      node->addComponent<LX_core::MaterialComponent>(std::move(material));
    }
  } catch (const std::exception& error) {
    return makeCommandError(std::string("failed to set materialUri: ") +
                            error.what());
  }
  documentNode->materialUri = uri;

  return makeCommandOk(
      "materialUri updated",
      "{\"path\":\"" + jsonEscape(path) + "\",\"materialUri\":\"" +
          jsonEscape(uri) + "\"}");
}

LX_core::CommandResult
SceneRuntime::setNodeMaterialBaseColor(const std::string& path,
                                       const LX_core::Vec3f& color) {
  const auto runtime = requireRuntimeData(m_impl);
  auto* documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode* node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.baseColor = color;
    auto material = loadMaterialForSceneNode(uri, documentNode->materialOverrides,
                                            nodeOverrides);
    if (!materialHasBaseColor(material)) {
      return makeCommandError("material does not expose MaterialUBO.baseColor: " +
                              uri);
    }
    materialComponent->get().setMaterialInstance(std::move(material));
    documentNode->materialUri = uri;
    documentNode->nodeMaterialOverrides = nodeOverrides;
  } catch (const std::exception& error) {
    return makeCommandError(std::string("failed to set node material baseColor: ") +
                            error.what());
  }

  return makeCommandOk("node material baseColor updated",
                       "{\"path\":\"" + jsonEscape(path) +
                           "\",\"baseColor\":" + makeVec3Json(color) + "}");
}

LX_core::CommandResult
SceneRuntime::applyMaterialOverride(const std::string& path,
                                    const std::string& field) {
  if (field != "baseColor") {
    return makeCommandError("unknown material override field: " + field);
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto* documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  const auto color = documentNode->nodeMaterialOverrides.baseColor;
  if (!color.has_value()) {
    return makeCommandError("node has no baseColor override: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  usize updatedDocuments = 0;
  forEachDocumentNode(runtime->document.mutableRootNode(),
                      [&](SceneNodeDocument& candidate) {
                        if (!documentNodeHasMaterialSurface(candidate)) {
                          return;
                        }
                        if (normalizeMaterialUri(candidate) != uri) {
                          return;
                        }
                        candidate.materialUri = uri;
                        candidate.materialOverrides.baseColor = *color;
                        ++updatedDocuments;
                      });

  usize updatedRuntimeNodes = 0;
  forEachRuntimeNode(runtime->scene->getRootNode(), [&](LX_core::SceneNode& node) {
    auto* candidateDocument =
        findDocumentNodeByName(runtime->document.mutableRootNode(),
                               node.getNodeName());
    if (!candidateDocument || !documentNodeHasMaterialSurface(*candidateDocument) ||
        normalizeMaterialUri(*candidateDocument) != uri) {
      return;
    }
    const auto materialComponent =
        node.getComponent<LX_core::MaterialComponent>();
    if (!materialComponent.has_value()) {
      return;
    }
    const auto effectiveNodeOverride =
        candidateDocument->nodeMaterialOverrides.baseColor;
    try {
      auto material = loadMaterialForSceneNode(
          uri, candidateDocument->materialOverrides,
          MaterialOverrideState{.baseColor = effectiveNodeOverride});
      materialComponent->get().setMaterialInstance(std::move(material));
      ++updatedRuntimeNodes;
    } catch (const std::exception& error) {
      std::cerr << "[lxe_editor] failed to apply material override to "
                << node.getPath() << ": " << error.what() << "\n";
    }
  });

  return makeCommandOk(
      "material baseColor override applied",
      "{\"materialUri\":\"" + jsonEscape(uri) + "\",\"updatedDocuments\":" +
          std::to_string(updatedDocuments) + ",\"updatedRuntimeNodes\":" +
          std::to_string(updatedRuntimeNodes) + ",\"baseColor\":" +
          makeVec3Json(*color) + "}");
}

} // namespace LX_demo::lxe_editor
