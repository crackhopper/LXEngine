#include "demos/lxe_editor/project_document.hpp"

#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace LX_demo::lxe_editor {
namespace {

constexpr const char *kProjectTemplateSchema = "lxe.project_template.v1";
constexpr const char *kProjectSchema = "lxe.project.v1";

[[nodiscard]] const YAML::Node requireMap(const YAML::Node &node,
                                          const char *documentName) {
  if (!node || !node.IsMap()) {
    throw std::runtime_error(std::string("expected map for ") + documentName);
  }
  return node;
}

[[nodiscard]] std::string requireString(const YAML::Node &node,
                                        const char *fieldName) {
  if (!node || !node.IsScalar()) {
    throw std::runtime_error(std::string("expected string for ") + fieldName);
  }
  const std::string value = node.as<std::string>();
  if (value.empty()) {
    throw std::runtime_error(std::string("expected non-empty string for ") +
                             fieldName);
  }
  return value;
}

void requireSchema(const YAML::Node &root, const char *expectedSchema,
                   const char *documentName) {
  const std::string schema = requireString(root["schema"], "schema");
  if (schema != expectedSchema) {
    throw std::runtime_error(std::string("unsupported ") + documentName +
                             " schema: " + schema);
  }
}

[[nodiscard]] std::filesystem::path requirePath(const YAML::Node &node,
                                                const char *fieldName) {
  return std::filesystem::path(requireString(node, fieldName));
}

[[nodiscard]] std::vector<std::filesystem::path>
loadPathSequence(const YAML::Node &node, const char *fieldName) {
  std::vector<std::filesystem::path> paths;
  if (!node) {
    return paths;
  }
  if (!node.IsSequence()) {
    throw std::runtime_error(std::string("expected sequence for ") + fieldName);
  }
  for (const auto &entryNode : node) {
    paths.push_back(requirePath(entryNode, fieldName));
  }
  return paths;
}

[[nodiscard]] ProjectSceneEntry loadSceneEntry(const YAML::Node &node) {
  if (!node || !node.IsMap()) {
    throw std::runtime_error("expected map for scenes[]");
  }

  ProjectSceneEntry entry;
  entry.id = requireString(node["id"], "scenes[].id");
  entry.path = requirePath(node["path"], "scenes[].path");
  return entry;
}

[[nodiscard]] std::vector<ProjectSceneEntry>
loadSceneEntries(const YAML::Node &node) {
  std::vector<ProjectSceneEntry> scenes;
  if (!node) {
    return scenes;
  }
  if (!node.IsSequence()) {
    throw std::runtime_error("expected sequence for scenes");
  }
  for (const auto &entryNode : node) {
    scenes.push_back(loadSceneEntry(entryNode));
  }
  return scenes;
}

void savePathSequence(YAML::Emitter &out, const char *key,
                      const std::vector<std::filesystem::path> &paths) {
  out << YAML::Key << key << YAML::Value << YAML::BeginSeq;
  for (const auto &path : paths) {
    out << path.generic_string();
  }
  out << YAML::EndSeq;
}

[[nodiscard]] bool canSaveProjectDocument(const ProjectDocument &document) {
  if (document.id.empty() || document.displayName.empty() ||
      document.activeScene.empty()) {
    return false;
  }
  for (const auto &scene : document.scenes) {
    if (scene.id.empty() || scene.path.empty()) {
      return false;
    }
  }
  for (const auto &assetRoot : document.assetRoots) {
    if (assetRoot.empty()) {
      return false;
    }
  }
  if (document.createdFromTemplate.has_value() &&
      document.createdFromTemplate->empty()) {
    return false;
  }
  return true;
}

} // namespace

ProjectTemplateDocument
loadProjectTemplateDocument(const std::filesystem::path &path) {
  const YAML::Node root =
      requireMap(YAML::LoadFile(path.string()), "project template document");
  requireSchema(root, kProjectTemplateSchema, "project template");

  ProjectTemplateDocument document;
  document.schema = kProjectTemplateSchema;
  document.id = requireString(root["id"], "id");
  document.displayName = requireString(root["displayName"], "displayName");
  document.defaultScene = requirePath(root["defaultScene"], "defaultScene");
  document.copyRoots = loadPathSequence(root["copy"], "copy");
  document.scenes = loadSceneEntries(root["scenes"]);
  return document;
}

ProjectDocument loadProjectDocument(const std::filesystem::path &path) {
  const YAML::Node root =
      requireMap(YAML::LoadFile(path.string()), "project document");
  requireSchema(root, kProjectSchema, "project");

  ProjectDocument document;
  document.schema = kProjectSchema;
  document.id = requireString(root["id"], "id");
  document.displayName = requireString(root["displayName"], "displayName");
  document.activeScene = requirePath(root["activeScene"], "activeScene");
  document.scenes = loadSceneEntries(root["scenes"]);
  document.assetRoots = loadPathSequence(root["assetRoots"], "assetRoots");
  if (const auto templateNode = root["createdFromTemplate"]; templateNode) {
    document.createdFromTemplate =
        requireString(templateNode, "createdFromTemplate");
  }
  return document;
}

bool saveProjectDocument(const std::filesystem::path &path,
                         const ProjectDocument &document) {
  if (!canSaveProjectDocument(document)) {
    std::cerr << "[lxe_editor] refusing to write incomplete project document "
              << path << "\n";
    return false;
  }

  std::error_code ec;
  const auto parentPath = path.parent_path();
  if (!parentPath.empty()) {
    std::filesystem::create_directories(parentPath, ec);
    if (ec) {
      std::cerr << "[lxe_editor] failed to create project document directory "
                << parentPath << ": " << ec.message() << "\n";
      return false;
    }
  }

  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "schema" << YAML::Value << kProjectSchema;
  out << YAML::Key << "id" << YAML::Value << document.id;
  out << YAML::Key << "displayName" << YAML::Value << document.displayName;
  out << YAML::Key << "activeScene" << YAML::Value
      << document.activeScene.generic_string();
  out << YAML::Key << "scenes" << YAML::Value << YAML::BeginSeq;
  for (const auto &scene : document.scenes) {
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << scene.id;
    out << YAML::Key << "path" << YAML::Value << scene.path.generic_string();
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
  savePathSequence(out, "assetRoots", document.assetRoots);
  if (document.createdFromTemplate.has_value()) {
    out << YAML::Key << "createdFromTemplate" << YAML::Value
        << *document.createdFromTemplate;
  }
  out << YAML::EndMap;

  std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    std::cerr << "[lxe_editor] failed to open project document for write "
              << path << "\n";
    return false;
  }
  file << out.c_str();
  return static_cast<bool>(file);
}

} // namespace LX_demo::lxe_editor
