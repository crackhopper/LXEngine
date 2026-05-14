#include "demos/lxe_editor/scene_document.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace LX_demo::lxe_editor {
namespace {

constexpr const char* kDefaultRootNodeName = "scene_root";

[[nodiscard]] SceneNodeDocument makeDefaultRootNode() {
  SceneNodeDocument rootNode;
  rootNode.nodeName = kDefaultRootNodeName;
  return rootNode;
}

struct SceneDocumentData final {
  std::string sceneName = "Scene";
  std::string gameplayCameraPath = "/game_cam";
  SceneNodeDocument rootNode;
  std::optional<EditorCameraState> editorCamera;

  SceneDocumentData() : rootNode(makeDefaultRootNode()) {}
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

void saveVec3(YAML::Emitter& out, const LX_core::Vec3f& value) {
  out << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z
      << YAML::EndSeq;
}

[[nodiscard]] LX_core::Quatf loadQuat(const YAML::Node& node,
                                      const char* fieldName) {
  if (!node || !node.IsSequence() || node.size() != 4) {
    throw std::runtime_error(std::string("expected quat sequence for ") +
                             fieldName);
  }
  return LX_core::Quatf{node[0].as<float>(), node[1].as<float>(),
                        node[2].as<float>(), node[3].as<float>()}
      .normalized();
}

void saveQuat(YAML::Emitter& out, const LX_core::Quatf& value) {
  out << YAML::Flow << YAML::BeginSeq << value.w << value.v.x << value.v.y
      << value.v.z << YAML::EndSeq;
}

[[nodiscard]] CameraNodeState loadCameraState(const YAML::Node& node) {
  CameraNodeState state;
  state.eye = loadVec3(node["eye"], "nodes[].camera.eye");
  state.target = loadVec3(node["target"], "nodes[].camera.target");
  state.up = loadVec3(node["up"], "nodes[].camera.up");
  if (const auto typeNode = node["type"]; typeNode) {
    const std::string type = typeNode.as<std::string>();
    if (type == "orthographic") {
      state.type = LX_core::CameraType::Orthographic;
    } else {
      state.type = LX_core::CameraType::Perspective;
    }
  }
  state.fovY = node["fovY"].as<float>();
  state.aspect = node["aspect"].as<float>();
  state.nearPlane = node["nearPlane"].as<float>();
  state.farPlane = node["farPlane"].as<float>();
  state.left = node["left"].as<float>();
  state.right = node["right"].as<float>();
  state.bottom = node["bottom"].as<float>();
  state.top = node["top"].as<float>();
  state.cullingMask = node["cullingMask"].as<LX_core::VisibilityLayerMask>();
  return state;
}

void saveCameraState(YAML::Emitter& out, const CameraNodeState& state) {
  out << YAML::Key << "camera" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "eye" << YAML::Value;
  saveVec3(out, state.eye);
  out << YAML::Key << "target" << YAML::Value;
  saveVec3(out, state.target);
  out << YAML::Key << "up" << YAML::Value;
  saveVec3(out, state.up);
  out << YAML::Key << "type" << YAML::Value
      << (state.type == LX_core::CameraType::Orthographic ? "orthographic"
                                                          : "perspective");
  out << YAML::Key << "fovY" << YAML::Value << state.fovY;
  out << YAML::Key << "aspect" << YAML::Value << state.aspect;
  out << YAML::Key << "nearPlane" << YAML::Value << state.nearPlane;
  out << YAML::Key << "farPlane" << YAML::Value << state.farPlane;
  out << YAML::Key << "left" << YAML::Value << state.left;
  out << YAML::Key << "right" << YAML::Value << state.right;
  out << YAML::Key << "bottom" << YAML::Value << state.bottom;
  out << YAML::Key << "top" << YAML::Value << state.top;
  out << YAML::Key << "cullingMask" << YAML::Value << state.cullingMask;
  out << YAML::EndMap;
}

[[nodiscard]] LightKind loadLightKind(const YAML::Node& node) {
  const std::string kind = node.as<std::string>();
  if (kind == "Directional") {
    return LightKind::Directional;
  }
  if (kind == "Point") {
    return LightKind::Point;
  }
  if (kind == "Spot") {
    return LightKind::Spot;
  }
  throw std::runtime_error("unsupported light kind: " + kind);
}

[[nodiscard]] const char* lightKindName(const LightKind kind) {
  switch (kind) {
  case LightKind::Directional:
    return "Directional";
  case LightKind::Point:
    return "Point";
  case LightKind::Spot:
    return "Spot";
  }
  return "Directional";
}

[[nodiscard]] LightNodeState
loadLegacyDirectionalLightState(const YAML::Node& node) {
  LightNodeState state;
  state.kind = LightKind::Directional;
  state.direction = loadVec3(node["direction"], "nodes[].directionalLight.direction");
  state.color = loadVec3(node["color"], "nodes[].directionalLight.color");
  state.intensity = node["intensity"].as<float>();
  return state;
}

[[nodiscard]] LightNodeState loadLightState(const YAML::Node& node) {
  LightNodeState state;
  state.kind = loadLightKind(node["kind"]);
  if (state.kind == LightKind::Directional || state.kind == LightKind::Spot) {
    state.direction = loadVec3(node["direction"], "nodes[].light.direction");
  }
  state.color = loadVec3(node["color"], "nodes[].light.color");
  state.intensity = node["intensity"].as<float>();
  if (state.kind == LightKind::Point || state.kind == LightKind::Spot) {
    state.range = node["range"].as<float>();
  }
  if (state.kind == LightKind::Spot) {
    state.innerConeDegrees = node["innerConeDegrees"].as<float>();
    state.outerConeDegrees = node["outerConeDegrees"].as<float>();
  }
  return state;
}

void saveLightState(YAML::Emitter& out, const LightNodeState& state) {
  out << YAML::Key << "light" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "kind" << YAML::Value << lightKindName(state.kind);
  if (state.kind == LightKind::Directional || state.kind == LightKind::Spot) {
    out << YAML::Key << "direction" << YAML::Value;
    saveVec3(out, state.direction);
  }
  out << YAML::Key << "color" << YAML::Value;
  saveVec3(out, state.color);
  out << YAML::Key << "intensity" << YAML::Value << state.intensity;
  if (state.kind == LightKind::Point || state.kind == LightKind::Spot) {
    out << YAML::Key << "range" << YAML::Value << state.range;
  }
  if (state.kind == LightKind::Spot) {
    out << YAML::Key << "innerConeDegrees" << YAML::Value
        << state.innerConeDegrees;
    out << YAML::Key << "outerConeDegrees" << YAML::Value
        << state.outerConeDegrees;
  }
  out << YAML::EndMap;
}

[[nodiscard]] MaterialOverrideState loadMaterialOverrideState(
    const YAML::Node& node, const char* fieldName) {
  MaterialOverrideState state;
  if (!node) {
    return state;
  }
  if (!node.IsMap()) {
    throw std::runtime_error(std::string("expected map for ") + fieldName);
  }
  if (const auto baseColorNode = node["baseColor"]; baseColorNode) {
    state.baseColor = loadVec3(baseColorNode, fieldName);
  }
  return state;
}

void saveMaterialOverrideState(YAML::Emitter& out, const char* key,
                               const MaterialOverrideState& state) {
  if (!state.baseColor.has_value()) {
    return;
  }
  out << YAML::Key << key << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "baseColor" << YAML::Value;
  saveVec3(out, *state.baseColor);
  out << YAML::EndMap;
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

void saveEditorCamera(YAML::Emitter& out, const EditorCameraState& state) {
  out << YAML::Key << "editor" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "editorCamera" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "position" << YAML::Value;
  saveVec3(out, state.position);
  out << YAML::Key << "rotationEulerDeg" << YAML::Value;
  saveVec3(out, state.rotationEulerDeg);
  out << YAML::Key << "fovY" << YAML::Value << state.fovY;
  out << YAML::Key << "nearPlane" << YAML::Value << state.nearPlane;
  out << YAML::Key << "farPlane" << YAML::Value << state.farPlane;
  out << YAML::EndMap;
  out << YAML::EndMap;
}

[[nodiscard]] SceneNodeDocument loadNodeDocument(const YAML::Node& node) {
  SceneNodeDocument entry;
  entry.nodeName =
      node["nodeName"] ? node["nodeName"].as<std::string>() : std::string{};
  if (entry.nodeName.empty()) {
    throw std::runtime_error("scene document node is missing nodeName");
  }
  entry.name = node["name"] ? node["name"].as<std::string>() : std::string{};
  entry.parentPath =
      node["parentPath"] ? node["parentPath"].as<std::string>() : std::string{};
  if (const auto transformNode = node["transform"]; transformNode) {
    entry.transform.translation =
        loadVec3(transformNode["translation"], "nodes[].transform.translation");
    entry.transform.rotation =
        loadQuat(transformNode["rotation"], "nodes[].transform.rotation");
    entry.transform.scale =
        loadVec3(transformNode["scale"], "nodes[].transform.scale");
  }
  if (const auto visibilityNode = node["visibilityMask"]; visibilityNode) {
    entry.visibilityMask = visibilityNode.as<LX_core::VisibilityLayerMask>();
  }
  if (const auto meshNode = node["mesh"]; meshNode && meshNode["uri"]) {
    entry.meshUri = meshNode["uri"].as<std::string>();
  }
  if (const auto materialNode = node["material"];
      materialNode && materialNode["uri"]) {
    entry.materialUri = materialNode["uri"].as<std::string>();
  }
  entry.nodeMaterialOverrides = loadMaterialOverrideState(
      node["nodeMaterialOverrides"], "nodes[].nodeMaterialOverrides");
  entry.materialOverrides = loadMaterialOverrideState(
      node["materialOverrides"], "nodes[].materialOverrides");
  if (const auto cameraNode = node["camera"]; cameraNode) {
    entry.camera = loadCameraState(cameraNode);
  }
  if (const auto lightNode = node["light"]; lightNode) {
    entry.light = loadLightState(lightNode);
  } else if (const auto legacyLightNode = node["directionalLight"];
             legacyLightNode) {
    entry.light = loadLegacyDirectionalLightState(legacyLightNode);
  }
  if (const auto childrenNode = node["children"];
      childrenNode) {
    if (!childrenNode.IsSequence()) {
      throw std::runtime_error("scene document children must be a sequence");
    }
    entry.children.reserve(childrenNode.size());
    for (const auto& childNode : childrenNode) {
      entry.children.push_back(loadNodeDocument(childNode));
    }
  }
  return entry;
}

void validateExplicitRootNode(const SceneNodeDocument& rootNode) {
  if (rootNode.nodeName != kDefaultRootNodeName) {
    throw std::runtime_error(
        "scene document root identity must use canonical nodeName 'scene_root'");
  }
  if (!rootNode.name.empty() || !rootNode.parentPath.empty()) {
    throw std::runtime_error(
        "scene document root identity must use empty name and no parentPath");
  }
  if (rootNode.meshUri.has_value() || rootNode.materialUri.has_value() ||
      rootNode.nodeMaterialOverrides.baseColor.has_value() ||
      rootNode.materialOverrides.baseColor.has_value() ||
      rootNode.camera.has_value() || rootNode.light.has_value()) {
    throw std::runtime_error(
        "scene document root payload is unsupported");
  }
}

void saveNodeDocument(YAML::Emitter& out, const SceneNodeDocument& node) {
  out << YAML::BeginMap;
  out << YAML::Key << "nodeName" << YAML::Value << node.nodeName;
  out << YAML::Key << "name" << YAML::Value << node.name;
  out << YAML::Key << "transform" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "translation" << YAML::Value;
  saveVec3(out, node.transform.translation);
  out << YAML::Key << "rotation" << YAML::Value;
  saveQuat(out, node.transform.rotation);
  out << YAML::Key << "scale" << YAML::Value;
  saveVec3(out, node.transform.scale);
  out << YAML::EndMap;
  out << YAML::Key << "visibilityMask" << YAML::Value << node.visibilityMask;
  if (node.meshUri.has_value()) {
    out << YAML::Key << "mesh" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "uri" << YAML::Value << *node.meshUri;
    out << YAML::EndMap;
  }
  if (node.materialUri.has_value()) {
    out << YAML::Key << "material" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "uri" << YAML::Value << *node.materialUri;
    out << YAML::EndMap;
  }
  saveMaterialOverrideState(out, "nodeMaterialOverrides",
                            node.nodeMaterialOverrides);
  saveMaterialOverrideState(out, "materialOverrides", node.materialOverrides);
  if (node.camera.has_value()) {
    saveCameraState(out, *node.camera);
  }
  if (node.light.has_value()) {
    saveLightState(out, *node.light);
  }
  if (!node.children.empty()) {
    out << YAML::Key << "children" << YAML::Value << YAML::BeginSeq;
    for (const auto& child : node.children) {
      saveNodeDocument(out, child);
    }
    out << YAML::EndSeq;
  }
  out << YAML::EndMap;
}

[[nodiscard]] std::vector<std::string> splitPathSegments(std::string path) {
  if (path.empty()) {
    path = "/";
  } else if (path.front() != '/') {
    path.insert(path.begin(), '/');
  }

  std::vector<std::string> segments;
  std::string current;
  for (usize i = 1; i < path.size(); ++i) {
    if (path[i] == '/') {
      segments.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(path[i]);
  }
  if (path.size() > 1) {
    segments.push_back(current);
  }
  return segments;
}

[[nodiscard]] SceneNodeDocument* findNodeByPath(SceneNodeDocument& rootNode,
                                                const std::string& path) {
  const auto segments = splitPathSegments(path);
  SceneNodeDocument* current = &rootNode;
  for (const auto& segment : segments) {
    auto childIt = std::find_if(
        current->children.begin(), current->children.end(),
        [&segment](const SceneNodeDocument& child) {
          return child.name == segment || child.nodeName == segment;
        });
    if (childIt == current->children.end()) {
      return nullptr;
    }
    current = &*childIt;
  }
  return current;
}

[[nodiscard]] std::string canonicalPathSegment(const SceneNodeDocument& node) {
  return node.name.empty() ? node.nodeName : node.name;
}

[[nodiscard]] std::string canonicalizePath(const SceneNodeDocument& rootNode,
                                           const std::string& path) {
  const auto segments = splitPathSegments(path);
  if (segments.empty()) {
    return "/";
  }

  const SceneNodeDocument* current = &rootNode;
  std::string canonicalPath;
  for (const auto& segment : segments) {
    const auto childIt = std::find_if(
        current->children.begin(), current->children.end(),
        [&segment](const SceneNodeDocument& child) {
          return child.name == segment || child.nodeName == segment;
        });
    if (childIt == current->children.end()) {
      return path;
    }

    canonicalPath += "/";
    canonicalPath += canonicalPathSegment(*childIt);
    current = &*childIt;
  }

  return canonicalPath;
}

[[nodiscard]] SceneNodeDocument
normalizeLegacyNodes(std::vector<SceneNodeDocument> flatNodes) {
  SceneNodeDocument rootNode = makeDefaultRootNode();

  while (!flatNodes.empty()) {
    bool attachedAny = false;
    for (auto it = flatNodes.begin(); it != flatNodes.end();) {
      const std::string normalizedParentPath =
          it->parentPath.empty() ? "/" : it->parentPath;
      SceneNodeDocument* parent = findNodeByPath(rootNode, normalizedParentPath);
      if (parent == nullptr) {
        ++it;
        continue;
      }

      it->parentPath.clear();
      parent->children.push_back(std::move(*it));
      it = flatNodes.erase(it);
      attachedAny = true;
    }
    if (!attachedAny) {
      throw std::runtime_error(
          "scene document parent path not found during legacy normalization");
    }
  }

  return rootNode;
}

} // namespace

SceneDocument::SceneDocument(const SceneDocument& other) {
  if (other.m_impl) {
    m_impl = std::make_shared<SceneDocumentData>(
        *std::static_pointer_cast<const SceneDocumentData>(other.m_impl));
  }
}

SceneDocument& SceneDocument::operator=(const SceneDocument& other) {
  if (this == &other) {
    return *this;
  }

  if (!other.m_impl) {
    m_impl.reset();
    return *this;
  }

  m_impl = std::make_shared<SceneDocumentData>(
      *std::static_pointer_cast<const SceneDocumentData>(other.m_impl));
  return *this;
}

const std::string& SceneDocument::sceneName() const {
  static const std::string kDefaultSceneName = "Scene";
  if (!m_impl) {
    return kDefaultSceneName;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)->sceneName;
}

void SceneDocument::setSceneName(std::string sceneName) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->sceneName =
      std::move(sceneName);
}

void SceneDocument::setGameplayCameraPath(std::string path) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->gameplayCameraPath =
      std::move(path);
}

const std::string& SceneDocument::gameplayCameraPath() const {
  static const std::string kDefaultGameplayCameraPath = "/game_cam";
  if (!m_impl) {
    return kDefaultGameplayCameraPath;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)
      ->gameplayCameraPath;
}

SceneNodeDocument& SceneDocument::mutableRootNode() {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  return std::static_pointer_cast<SceneDocumentData>(m_impl)->rootNode;
}

const SceneNodeDocument& SceneDocument::rootNode() const {
  static const SceneNodeDocument kDefaultRootNode = makeDefaultRootNode();
  if (!m_impl) {
    return kDefaultRootNode;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)->rootNode;
}

bool SceneDocument::hasEditorCamera() const {
  return m_impl &&
         std::static_pointer_cast<const SceneDocumentData>(m_impl)
             ->editorCamera.has_value();
}

const EditorCameraState& SceneDocument::editorCamera() const {
  if (!m_impl) {
    throw std::runtime_error("scene document has no editor camera");
  }
  const auto& state =
      std::static_pointer_cast<const SceneDocumentData>(m_impl)->editorCamera;
  if (!state.has_value()) {
    throw std::runtime_error("scene document has no editor camera");
  }
  return *state;
}

void SceneDocument::setEditorCamera(const EditorCameraState& state) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->editorCamera = state;
}

SceneDocument loadSceneDocument(const std::filesystem::path& path) {
  const YAML::Node root = YAML::LoadFile(path.string());

  SceneDocument document;
  if (const YAML::Node sceneNode = root["scene"]; sceneNode) {
    if (const YAML::Node nameNode = sceneNode["name"]; nameNode) {
      document.setSceneName(nameNode.as<std::string>());
    }
    if (const YAML::Node gameplayCameraPathNode =
            sceneNode["gameplayCameraPath"];
        gameplayCameraPathNode) {
      document.setGameplayCameraPath(gameplayCameraPathNode.as<std::string>());
    }
  }

  if (const YAML::Node rootNode = root["root"]; rootNode.IsDefined()) {
    if (!rootNode.IsMap()) {
      throw std::runtime_error("scene document root must be a map");
    }
    auto parsedRoot = loadNodeDocument(rootNode);
    validateExplicitRootNode(parsedRoot);
    document.mutableRootNode() = std::move(parsedRoot);
  } else if (const YAML::Node nodesNode = root["nodes"];
             nodesNode && nodesNode.IsSequence()) {
    std::vector<SceneNodeDocument> flatNodes;
    flatNodes.reserve(nodesNode.size());
    for (const auto& node : nodesNode) {
      flatNodes.push_back(loadNodeDocument(node));
    }
    document.mutableRootNode() = normalizeLegacyNodes(std::move(flatNodes));
    document.setGameplayCameraPath(
        canonicalizePath(document.rootNode(), document.gameplayCameraPath()));
  }

  if (const YAML::Node editorNode = root["editor"]; editorNode) {
    if (const auto editorCamera = loadEditorCamera(editorNode["editorCamera"]);
        editorCamera.has_value()) {
      document.setEditorCamera(*editorCamera);
    }
  }

  return document;
}

void saveSceneDocument(const std::filesystem::path& path,
                       const SceneDocument& document) {
  validateExplicitRootNode(document.rootNode());

  if (const auto parentPath = path.parent_path(); !parentPath.empty()) {
    std::filesystem::create_directories(parentPath);
  }

  YAML::Emitter out;
  out << YAML::BeginMap;

  out << YAML::Key << "scene" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "name" << YAML::Value << document.sceneName();
  out << YAML::Key << "gameplayCameraPath" << YAML::Value
      << document.gameplayCameraPath();
  out << YAML::EndMap;

  out << YAML::Key << "root" << YAML::Value;
  saveNodeDocument(out, document.rootNode());

  if (document.hasEditorCamera()) {
    saveEditorCamera(out, document.editorCamera());
  }

  out << YAML::EndMap;

  std::ofstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open scene document for write: " +
                             path.string());
  }
  stream << out.c_str() << '\n';
}

} // namespace LX_demo::lxe_editor
