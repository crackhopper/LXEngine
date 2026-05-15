#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct ProjectSceneEntry final {
  std::string id;
  std::filesystem::path path;
};

struct ProjectTemplateDocument final {
  std::string schema = "lxe.project_template.v1";
  std::string id;
  std::string displayName;
  std::filesystem::path defaultScene;
  std::vector<std::filesystem::path> copyRoots;
};

struct ProjectDocument final {
  std::string schema = "lxe.project.v1";
  std::string id;
  std::string displayName;
  std::filesystem::path activeScene;
  std::vector<ProjectSceneEntry> scenes;
  std::vector<std::filesystem::path> assetRoots;
  std::optional<std::string> createdFromTemplate;
};

[[nodiscard]] ProjectTemplateDocument
loadProjectTemplateDocument(const std::filesystem::path &path);
[[nodiscard]] ProjectDocument
loadProjectDocument(const std::filesystem::path &path);
bool saveProjectDocument(const std::filesystem::path &path,
                         const ProjectDocument &document);

} // namespace LX_demo::lxe_editor
