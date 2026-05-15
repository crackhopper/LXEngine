#include "demos/lxe_editor/project_session.hpp"

#include "demos/lxe_editor/project_catalog.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::filesystem::path
absoluteNormal(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::string escapeJsonString(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20U) {
        char buffer[7] = {};
        std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                      static_cast<unsigned int>(
                          static_cast<unsigned char>(ch)));
        escaped += buffer;
      } else {
        escaped += ch;
      }
      break;
    }
  }
  return escaped;
}

[[nodiscard]] ProjectCommandResult makeResult(
    bool ok, std::string message, const std::filesystem::path &path = {},
    const std::optional<std::string> &projectId = std::nullopt) {
  std::string json = std::string("{\"ok\":") + (ok ? "true" : "false");
  if (projectId.has_value()) {
    json += ",\"projectId\":\"" + escapeJsonString(*projectId) + "\"";
  }
  if (!path.empty()) {
    json += ",\"path\":\"" + escapeJsonString(path.generic_string()) + "\"";
  }
  json += "}";
  return {ok, std::move(message), std::move(json)};
}

[[nodiscard]] std::string sceneIdFromPath(const std::filesystem::path &path) {
  std::string filename = path.filename().generic_string();
  constexpr const char *kSceneSuffix = ".scene.yaml";
  const std::string suffix(kSceneSuffix);
  if (filename.size() > suffix.size() &&
      filename.compare(filename.size() - suffix.size(), suffix.size(),
                       suffix) == 0) {
    filename.resize(filename.size() - suffix.size());
  } else {
    filename = path.stem().generic_string();
  }
  return filename.empty() ? "main" : filename;
}

[[nodiscard]] std::filesystem::path
relativeToRoot(const std::filesystem::path &root,
               const std::filesystem::path &path) {
  const auto normalizedRoot = absoluteNormal(root);
  const auto normalizedPath = absoluteNormal(path);
  auto relative = normalizedPath.lexically_relative(normalizedRoot);
  if (relative.empty()) {
    relative = path.lexically_normal();
  }
  return relative.lexically_normal();
}

[[nodiscard]] bool sceneIdExists(const ProjectDocument &document,
                                 const std::string &sceneId) {
  return std::any_of(document.scenes.begin(), document.scenes.end(),
                     [&sceneId](const ProjectSceneEntry &entry) {
                       return entry.id == sceneId;
                     });
}

[[nodiscard]] std::optional<ProjectSceneEntry>
findScene(const ProjectDocument &document, const std::string &sceneId) {
  const auto sceneIt =
      std::find_if(document.scenes.begin(), document.scenes.end(),
                   [&sceneId](const ProjectSceneEntry &entry) {
                     return entry.id == sceneId;
                   });
  if (sceneIt == document.scenes.end()) {
    return std::nullopt;
  }
  return *sceneIt;
}

[[nodiscard]] bool copyTemplateRoots(const std::filesystem::path &templatePath,
                                     const std::filesystem::path &projectPath,
                                     const ProjectTemplateDocument &document) {
  std::error_code ec;
  std::filesystem::create_directories(projectPath, ec);
  if (ec) {
    return false;
  }

  for (const auto &copyRoot : document.copyRoots) {
    const auto sourcePath = templatePath / copyRoot;
    if (!std::filesystem::exists(sourcePath)) {
      return false;
    }
    const auto targetPath = projectPath / copyRoot;
    if (sourcePath == targetPath) {
      continue;
    }
    std::filesystem::copy(sourcePath, targetPath,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
      return false;
    }
  }
  return true;
}

void writeMinimalScene(const std::filesystem::path &path,
                       const std::string &sceneId) {
  std::filesystem::create_directories(path.parent_path());
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "scene" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "name" << YAML::Value << sceneId;
  out << YAML::EndMap;
  out << YAML::Key << "nodes" << YAML::Value << YAML::BeginSeq
      << YAML::EndSeq;
  out << YAML::EndMap;

  std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("failed to create scene: " + path.string());
  }
  file << out.c_str();
  if (!file) {
    throw std::runtime_error("failed to write scene: " + path.string());
  }
}

} // namespace

ProjectSession::ProjectSession(std::filesystem::path templateRoot,
                               std::filesystem::path projectsRoot)
    : m_templateRoot(std::move(templateRoot)),
      m_projectsRoot(std::move(projectsRoot)) {}

bool ProjectSession::hasProject() const {
  return m_currentProject.has_value() && m_projectRoot.has_value();
}

bool ProjectSession::dirty() const { return m_dirty; }

void ProjectSession::setDirty(const bool dirty) { m_dirty = dirty; }

const std::optional<ProjectDocument> &ProjectSession::currentProject() const {
  return m_currentProject;
}

const std::optional<std::filesystem::path> &ProjectSession::projectRoot() const {
  return m_projectRoot;
}

std::optional<std::filesystem::path> ProjectSession::activeScenePath() const {
  if (!hasProject()) {
    return std::nullopt;
  }
  try {
    return resolveProjectScenePath(*m_projectRoot, *m_currentProject,
                                   m_currentProject->activeScene.string());
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

ProjectCommandResult
ProjectSession::initProject(const std::string &templateId,
                            const std::optional<std::string> &projectName) {
  ProjectTemplateCatalog templates(m_templateRoot);
  templates.refresh();
  const auto templateEntry = templates.findById(templateId);
  if (!templateEntry.has_value()) {
    return makeResult(false, "unknown project template: " + templateId);
  }

  try {
    const auto templateDocument =
        loadProjectTemplateDocument(templateEntry->path /
                                    "project_template.yaml");
    const std::string displayName =
        projectName.has_value() && !projectName->empty()
            ? *projectName
            : templateDocument.displayName;
    const auto projectPath =
        allocateProjectPath(m_projectsRoot, projectName.value_or(templateId));

    if (!copyTemplateRoots(templateEntry->path, projectPath,
                           templateDocument)) {
      return makeResult(false, "failed to copy project template", projectPath);
    }

    ProjectDocument document;
    document.id = makeProjectSlug(displayName);
    document.displayName = displayName;
    document.activeScene = templateDocument.defaultScene;
    document.scenes.push_back(
        {sceneIdFromPath(templateDocument.defaultScene),
         templateDocument.defaultScene.lexically_normal()});
    for (const auto &copyRoot : templateDocument.copyRoots) {
      const auto normalized = copyRoot.lexically_normal();
      if (!normalized.empty() && normalized.begin()->generic_string() ==
                                     std::filesystem::path("assets")
                                         .generic_string()) {
        document.assetRoots.push_back(normalized);
      }
    }
    document.createdFromTemplate = templateDocument.id;

    if (!saveProjectDocument(projectPath / "project.yaml", document)) {
      return makeResult(false, "failed to save project document", projectPath,
                        document.id);
    }

    m_currentProject = std::move(document);
    m_projectRoot = absoluteNormal(projectPath);
    m_dirty = false;
    return makeResult(true, "project initialized", *m_projectRoot,
                      m_currentProject->id);
  } catch (const std::exception &ex) {
    return makeResult(false, ex.what());
  }
}

ProjectCommandResult ProjectSession::openProject(const std::string &idOrPath) {
  ProjectCatalog projects(m_projectsRoot);
  projects.refresh();

  try {
    auto resolvedRoot = projects.resolveIdOrPath(idOrPath);
    if (resolvedRoot.filename() == "project.yaml") {
      resolvedRoot = resolvedRoot.parent_path();
    }
    auto document = loadProjectDocument(resolvedRoot / "project.yaml");
    m_projectRoot = absoluteNormal(resolvedRoot);
    m_currentProject = std::move(document);
    m_dirty = false;
    return makeResult(true, "project opened", *m_projectRoot,
                      m_currentProject->id);
  } catch (const std::exception &ex) {
    return makeResult(false, ex.what());
  }
}

ProjectCommandResult ProjectSession::saveProject() {
  if (!hasProject()) {
    return makeResult(false, "no project is open");
  }
  if (!saveProjectDocument(*m_projectRoot / "project.yaml",
                           *m_currentProject)) {
    return makeResult(false, "failed to save project document", *m_projectRoot,
                      m_currentProject->id);
  }
  m_dirty = false;
  return makeResult(true, "project saved", *m_projectRoot,
                    m_currentProject->id);
}

ProjectCommandResult ProjectSession::closeProject() {
  m_currentProject = std::nullopt;
  m_projectRoot = std::nullopt;
  m_dirty = false;
  return makeResult(true, "project closed");
}

ProjectCommandResult
ProjectSession::openScene(const std::string &sceneIdOrPath) {
  if (!hasProject()) {
    return makeResult(false, "no project is open");
  }

  try {
    const auto resolvedPath =
        resolveProjectScenePath(*m_projectRoot, *m_currentProject,
                                sceneIdOrPath);
    const auto relativePath = relativeToRoot(*m_projectRoot, resolvedPath);
    if (m_currentProject->activeScene != relativePath) {
      m_currentProject->activeScene = relativePath;
      m_dirty = true;
    }
    return makeResult(true, "scene opened", resolvedPath,
                      m_currentProject->id);
  } catch (const std::exception &ex) {
    return makeResult(false, ex.what(), {}, m_currentProject->id);
  }
}

ProjectCommandResult ProjectSession::newScene(const std::string &sceneId) {
  if (!hasProject()) {
    return makeResult(false, "no project is open");
  }
  if (sceneId.empty()) {
    return makeResult(false, "scene id is empty", {}, m_currentProject->id);
  }
  if (sceneIdExists(*m_currentProject, sceneId)) {
    return makeResult(false, "scene already exists: " + sceneId, {},
                      m_currentProject->id);
  }

  const auto relativePath =
      (std::filesystem::path("scenes") / (sceneId + ".scene.yaml"))
          .lexically_normal();
  const auto scenePath = *m_projectRoot / relativePath;
  if (std::filesystem::exists(scenePath)) {
    return makeResult(false, "scene file already exists", scenePath,
                      m_currentProject->id);
  }

  try {
    writeMinimalScene(scenePath, sceneId);
    m_currentProject->scenes.push_back({sceneId, relativePath});
    m_currentProject->activeScene = relativePath;
    m_dirty = true;
    return makeResult(true, "scene created", scenePath, m_currentProject->id);
  } catch (const std::exception &ex) {
    return makeResult(false, ex.what(), scenePath, m_currentProject->id);
  }
}

ProjectCommandResult ProjectSession::duplicateScene(
    const std::string &sourceSceneId, const std::string &newSceneId) {
  if (!hasProject()) {
    return makeResult(false, "no project is open");
  }
  if (newSceneId.empty()) {
    return makeResult(false, "scene id is empty", {}, m_currentProject->id);
  }
  if (sceneIdExists(*m_currentProject, newSceneId)) {
    return makeResult(false, "scene already exists: " + newSceneId, {},
                      m_currentProject->id);
  }

  try {
    const auto sourcePath =
        resolveProjectScenePath(*m_projectRoot, *m_currentProject,
                                sourceSceneId);
    const auto relativePath =
        (std::filesystem::path("scenes") / (newSceneId + ".scene.yaml"))
            .lexically_normal();
    const auto targetPath = *m_projectRoot / relativePath;
    if (std::filesystem::exists(targetPath)) {
      return makeResult(false, "scene file already exists", targetPath,
                        m_currentProject->id);
    }
    std::filesystem::create_directories(targetPath.parent_path());
    std::filesystem::copy_file(sourcePath, targetPath);
    m_currentProject->scenes.push_back({newSceneId, relativePath});
    m_currentProject->activeScene = relativePath;
    m_dirty = true;
    return makeResult(true, "scene duplicated", targetPath,
                      m_currentProject->id);
  } catch (const std::exception &ex) {
    return makeResult(false, ex.what(), {}, m_currentProject->id);
  }
}

ProjectCommandResult ProjectSession::removeScene(const std::string &sceneId) {
  if (!hasProject()) {
    return makeResult(false, "no project is open");
  }
  if (m_currentProject->scenes.size() <= 1) {
    return makeResult(false, "cannot remove the last scene", {},
                      m_currentProject->id);
  }

  const auto scene = findScene(*m_currentProject, sceneId);
  if (!scene.has_value()) {
    return makeResult(false, "unknown scene: " + sceneId, {},
                      m_currentProject->id);
  }
  if (scene->path == m_currentProject->activeScene) {
    return makeResult(false, "cannot remove the active scene", {},
                      m_currentProject->id);
  }

  try {
    const auto scenePath =
        resolveProjectScenePath(*m_projectRoot, *m_currentProject, sceneId);
    std::error_code ec;
    std::filesystem::remove(scenePath, ec);
    if (ec) {
      return makeResult(false, "failed to remove scene file", scenePath,
                        m_currentProject->id);
    }

    auto &scenes = m_currentProject->scenes;
    scenes.erase(std::remove_if(scenes.begin(), scenes.end(),
                                [&sceneId](const ProjectSceneEntry &entry) {
                                  return entry.id == sceneId;
                                }),
                 scenes.end());
    m_dirty = true;
    return makeResult(true, "scene removed", scenePath, m_currentProject->id);
  } catch (const std::exception &ex) {
    return makeResult(false, ex.what(), {}, m_currentProject->id);
  }
}

} // namespace LX_demo::lxe_editor
