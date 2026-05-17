#pragma once

#include "demos/lxe_editor/project_document.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace LX_demo::lxe_editor {

struct ProjectCommandResult final {
  bool ok = false;
  std::string message;
  std::string structuredJson;
};

class ProjectSession final {
public:
  ProjectSession(std::filesystem::path templateRoot,
                 std::filesystem::path projectsRoot);

  [[nodiscard]] bool hasProject() const;
  [[nodiscard]] bool dirty() const;
  void setDirty(bool dirty);
  [[nodiscard]] const std::optional<ProjectDocument> &currentProject() const;
  [[nodiscard]] const std::optional<std::filesystem::path> &projectRoot() const;
  [[nodiscard]] std::optional<std::filesystem::path> activeScenePath() const;

  [[nodiscard]] ProjectCommandResult
  initProject(const std::string &templateId,
              const std::optional<std::string> &projectName);
  [[nodiscard]] ProjectCommandResult openProject(const std::string &idOrPath);
  [[nodiscard]] ProjectCommandResult saveProject();
  [[nodiscard]] ProjectCommandResult closeProject();
  [[nodiscard]] ProjectCommandResult openScene(const std::string &sceneIdOrPath);
  [[nodiscard]] ProjectCommandResult newScene(const std::string &sceneId);
  [[nodiscard]] ProjectCommandResult duplicateScene(
      const std::string &sourceSceneId, const std::string &newSceneId);
  [[nodiscard]] ProjectCommandResult importScene(const std::string &sourcePath,
                                                 const std::string &sceneId);
  [[nodiscard]] ProjectCommandResult removeScene(const std::string &sceneId);

private:
  std::filesystem::path m_templateRoot;
  std::filesystem::path m_projectsRoot;
  std::optional<ProjectDocument> m_currentProject;
  std::optional<std::filesystem::path> m_projectRoot;
  bool m_dirty = false;
};

} // namespace LX_demo::lxe_editor
