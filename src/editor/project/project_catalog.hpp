#pragma once

#include "editor/project/project_document.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct ProjectTemplateCatalogEntry final {
  std::string id;
  std::string displayName;
  std::filesystem::path path;
};

struct ProjectCatalogEntry final {
  std::string id;
  std::string displayName;
  std::filesystem::path path;
};

class ProjectTemplateCatalog final {
public:
  explicit ProjectTemplateCatalog(std::filesystem::path root);
  void refresh();
  [[nodiscard]] const std::vector<ProjectTemplateCatalogEntry> &entries() const;
  [[nodiscard]] std::optional<ProjectTemplateCatalogEntry>
  findById(const std::string &id) const;

private:
  std::filesystem::path m_root;
  std::vector<ProjectTemplateCatalogEntry> m_entries;
};

class ProjectCatalog final {
public:
  explicit ProjectCatalog(std::filesystem::path root);
  void refresh();
  [[nodiscard]] const std::vector<ProjectCatalogEntry> &entries() const;
  [[nodiscard]] std::optional<ProjectCatalogEntry>
  findById(const std::string &id) const;
  [[nodiscard]] std::filesystem::path
  resolveIdOrPath(const std::string &token) const;

private:
  std::filesystem::path m_root;
  std::vector<ProjectCatalogEntry> m_entries;
};

[[nodiscard]] std::filesystem::path
resolveProjectScenePath(const std::filesystem::path &projectRoot,
                        const ProjectDocument &document,
                        const std::string &sceneIdOrPath);
[[nodiscard]] std::string makeProjectSlug(std::string name);
[[nodiscard]] std::filesystem::path
allocateProjectPath(const std::filesystem::path &projectsRoot,
                    const std::string &requestedName);

} // namespace LX_demo::lxe_editor
