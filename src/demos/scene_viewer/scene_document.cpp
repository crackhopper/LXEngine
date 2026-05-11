#include "demos/scene_viewer/scene_document.hpp"

#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace LX_demo::scene_viewer {
namespace {

struct SceneDocumentData final {
  std::string sceneName = "Scene";
  std::string gameplayCameraPath = "/game_cam";
  std::vector<SceneNodeDocument> nodes;
  std::optional<EditorCameraState> editorCamera;
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

[[nodiscard]] DirectionalLightNodeState
loadDirectionalLightState(const YAML::Node& node) {
  DirectionalLightNodeState state;
  state.direction = loadVec3(node["direction"], "nodes[].directionalLight.direction");
  state.color = loadVec3(node["color"], "nodes[].directionalLight.color");
  state.intensity = node["intensity"].as<float>();
  return state;
}

void saveDirectionalLightState(YAML::Emitter& out,
                               const DirectionalLightNodeState& state) {
  out << YAML::Key << "directionalLight" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "direction" << YAML::Value;
  saveVec3(out, state.direction);
  out << YAML::Key << "color" << YAML::Value;
  saveVec3(out, state.color);
  out << YAML::Key << "intensity" << YAML::Value << state.intensity;
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
  entry.nodeName = node["nodeName"].as<std::string>();
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
  if (const auto cameraNode = node["camera"]; cameraNode) {
    entry.camera = loadCameraState(cameraNode);
  }
  if (const auto lightNode = node["directionalLight"]; lightNode) {
    entry.directionalLight = loadDirectionalLightState(lightNode);
  }
  return entry;
}

void saveNodeDocument(YAML::Emitter& out, const SceneNodeDocument& node) {
  out << YAML::BeginMap;
  out << YAML::Key << "nodeName" << YAML::Value << node.nodeName;
  out << YAML::Key << "name" << YAML::Value << node.name;
  if (!node.parentPath.empty()) {
    out << YAML::Key << "parentPath" << YAML::Value << node.parentPath;
  }
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
  if (node.camera.has_value()) {
    saveCameraState(out, *node.camera);
  }
  if (node.directionalLight.has_value()) {
    saveDirectionalLightState(out, *node.directionalLight);
  }
  out << YAML::EndMap;
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

std::vector<SceneNodeDocument>& SceneDocument::mutableNodes() {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  return std::static_pointer_cast<SceneDocumentData>(m_impl)->nodes;
}

const std::vector<SceneNodeDocument>& SceneDocument::nodes() const {
  static const std::vector<SceneNodeDocument> kEmptyNodes;
  if (!m_impl) {
    return kEmptyNodes;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)->nodes;
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

  if (const YAML::Node nodesNode = root["nodes"]; nodesNode && nodesNode.IsSequence()) {
    auto& nodes = document.mutableNodes();
    nodes.clear();
    nodes.reserve(nodesNode.size());
    for (const auto& node : nodesNode) {
      nodes.push_back(loadNodeDocument(node));
    }
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

  out << YAML::Key << "nodes" << YAML::Value << YAML::BeginSeq;
  for (const auto& node : document.nodes()) {
    saveNodeDocument(out, node);
  }
  out << YAML::EndSeq;

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

} // namespace LX_demo::scene_viewer
