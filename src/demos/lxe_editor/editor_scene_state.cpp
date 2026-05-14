#include "demos/lxe_editor/editor_scene_state.hpp"

#include "yaml-cpp/yaml.h"

#include <fstream>
#include <stdexcept>

namespace LX_demo::lxe_editor {
namespace {

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

[[nodiscard]] EditorCameraState loadEditorCamera(const YAML::Node& node) {
  return EditorCameraState{
      .position = loadVec3(node["position"], "editorCamera.position"),
      .rotationEulerDeg =
          loadVec3(node["rotationEulerDeg"], "editorCamera.rotationEulerDeg"),
      .fovY = node["fovY"].as<float>(),
      .nearPlane = node["nearPlane"].as<float>(),
      .farPlane = node["farPlane"].as<float>(),
  };
}

void saveEditorCamera(YAML::Emitter& out, const EditorCameraState& state) {
  out << YAML::Key << "editorCamera" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "position" << YAML::Value;
  saveVec3(out, state.position);
  out << YAML::Key << "rotationEulerDeg" << YAML::Value;
  saveVec3(out, state.rotationEulerDeg);
  out << YAML::Key << "fovY" << YAML::Value << state.fovY;
  out << YAML::Key << "nearPlane" << YAML::Value << state.nearPlane;
  out << YAML::Key << "farPlane" << YAML::Value << state.farPlane;
  out << YAML::EndMap;
}

} // namespace

std::filesystem::path
editorSceneStatePathForScenePath(const std::filesystem::path& scenePath) {
  std::filesystem::path base = scenePath;
  if (base.extension() == ".yaml") {
    base.replace_extension();
  }
  if (base.extension() == ".scene") {
    base.replace_extension();
  }
  base += ".editor.yaml";
  return base;
}

std::optional<EditorSceneStateDocument>
loadEditorSceneStateIfPresent(const std::filesystem::path& scenePath) {
  const std::filesystem::path statePath =
      editorSceneStatePathForScenePath(scenePath);
  if (!std::filesystem::exists(statePath)) {
    return std::nullopt;
  }

  const YAML::Node root = YAML::LoadFile(statePath.string());
  EditorSceneStateDocument document;
  if (const YAML::Node cameraNode = root["editorCamera"]; cameraNode) {
    document.editorCamera = loadEditorCamera(cameraNode);
  }
  if (const YAML::Node orbitTargetNode = root["orbitTarget"];
      orbitTargetNode) {
    document.orbitTarget = loadVec3(orbitTargetNode, "orbitTarget");
  }
  if (const YAML::Node selectedNode = root["selection"];
      selectedNode && selectedNode["selectedPaths"]) {
    const YAML::Node pathsNode = selectedNode["selectedPaths"];
    if (!pathsNode.IsSequence()) {
      throw std::runtime_error("selection.selectedPaths must be a sequence");
    }
    document.selectedPaths.reserve(pathsNode.size());
    for (const auto& pathNode : pathsNode) {
      document.selectedPaths.push_back(pathNode.as<std::string>());
    }
  }
  return document;
}

void saveEditorSceneStateForScenePath(
    const std::filesystem::path& scenePath,
    const EditorSceneStateDocument& document) {
  const std::filesystem::path statePath =
      editorSceneStatePathForScenePath(scenePath);
  if (const auto parentPath = statePath.parent_path(); !parentPath.empty()) {
    std::filesystem::create_directories(parentPath);
  }

  YAML::Emitter out;
  out << YAML::BeginMap;
  if (document.editorCamera.has_value()) {
    saveEditorCamera(out, *document.editorCamera);
  }
  if (document.orbitTarget.has_value()) {
    out << YAML::Key << "orbitTarget" << YAML::Value;
    saveVec3(out, *document.orbitTarget);
  }
  out << YAML::Key << "selection" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "selectedPaths" << YAML::Value << YAML::BeginSeq;
  for (const auto& path : document.selectedPaths) {
    out << path;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;
  out << YAML::EndMap;

  std::ofstream stream(statePath);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open editor scene state for write: " +
                             statePath.string());
  }
  stream << out.c_str() << '\n';
}

} // namespace LX_demo::lxe_editor
