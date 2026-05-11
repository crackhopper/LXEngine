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
  std::string sceneName;
  GameCameraState gameCamera;
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

void saveGameCamera(YAML::Emitter& out, const GameCameraState& state) {
  out << YAML::Key << "gameCamera" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "eye" << YAML::Value;
  saveVec3(out, state.eye);
  out << YAML::Key << "target" << YAML::Value;
  saveVec3(out, state.target);
  out << YAML::Key << "up" << YAML::Value;
  saveVec3(out, state.up);
  out << YAML::Key << "fovY" << YAML::Value << state.fovY;
  out << YAML::Key << "nearPlane" << YAML::Value << state.nearPlane;
  out << YAML::Key << "farPlane" << YAML::Value << state.farPlane;
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
  static const std::string kEmptyName;
  if (!m_impl) {
    return kEmptyName;
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

GameCameraState& SceneDocument::mutableGameCamera() {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  return std::static_pointer_cast<SceneDocumentData>(m_impl)->gameCamera;
}

const GameCameraState& SceneDocument::gameCamera() const {
  static const GameCameraState kDefaultCamera;
  if (!m_impl) {
    return kDefaultCamera;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)->gameCamera;
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
  }

  document.mutableGameCamera() = loadGameCamera(root["gameCamera"]);

  if (const auto editorCamera = loadEditorCamera(root["editor"]["editorCamera"]);
      editorCamera.has_value()) {
    document.setEditorCamera(*editorCamera);
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
  out << YAML::EndMap;

  saveGameCamera(out, document.gameCamera());

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
