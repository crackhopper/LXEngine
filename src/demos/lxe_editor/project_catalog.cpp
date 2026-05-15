#include "demos/lxe_editor/project_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::filesystem::path
absoluteNormal(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::filesystem::path
weaklyCanonicalNormal(const std::filesystem::path &path) {
  std::error_code ec;
  const auto canonicalPath = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    return absoluteNormal(path);
  }
  return canonicalPath.lexically_normal();
}

[[nodiscard]] bool pathStartsWith(const std::filesystem::path &path,
                                  const std::filesystem::path &root) {
  const auto normalizedPath = path.lexically_normal();
  const auto normalizedRoot = root.lexically_normal();
  auto pathIt = normalizedPath.begin();
  const auto pathEnd = normalizedPath.end();
  for (auto rootIt = normalizedRoot.begin(); rootIt != normalizedRoot.end();
       ++rootIt, ++pathIt) {
    if (pathIt == pathEnd || *pathIt != *rootIt) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool catalogEntryLess(const ProjectTemplateCatalogEntry &lhs,
                                    const ProjectTemplateCatalogEntry &rhs) {
  if (lhs.id != rhs.id) {
    return lhs.id < rhs.id;
  }
  return lhs.path.generic_string() < rhs.path.generic_string();
}

[[nodiscard]] bool catalogEntryLess(const ProjectCatalogEntry &lhs,
                                    const ProjectCatalogEntry &rhs) {
  if (lhs.id != rhs.id) {
    return lhs.id < rhs.id;
  }
  return lhs.path.generic_string() < rhs.path.generic_string();
}

template <typename Entry>
void keepFirstEntryPerId(std::vector<Entry> &entries,
                         const char *catalogEntryName) {
  std::vector<Entry> uniqueEntries;
  uniqueEntries.reserve(entries.size());
  for (const auto &entry : entries) {
    if (!uniqueEntries.empty() && uniqueEntries.back().id == entry.id) {
      std::cerr << "[lxe_editor] skipping duplicate " << catalogEntryName
                << " id '" << entry.id << "' at " << entry.path << "; keeping "
                << uniqueEntries.back().path << "\n";
      continue;
    }
    uniqueEntries.push_back(entry);
  }
  entries = std::move(uniqueEntries);
}

[[nodiscard]] bool hasPathSeparator(const std::string &token) {
  return token.find('/') != std::string::npos ||
         token.find('\\') != std::string::npos;
}

template <typename Entry>
[[nodiscard]] std::optional<Entry>
findEntryById(const std::vector<Entry> &entries, const std::string &id) {
  const auto entryIt =
      std::find_if(entries.begin(), entries.end(),
                   [&id](const Entry &entry) { return entry.id == id; });
  if (entryIt == entries.end()) {
    return std::nullopt;
  }
  return *entryIt;
}

} // namespace

ProjectTemplateCatalog::ProjectTemplateCatalog(std::filesystem::path root)
    : m_root(std::move(root)) {}

void ProjectTemplateCatalog::refresh() {
  if (!std::filesystem::is_directory(m_root)) {
    m_entries.clear();
    return;
  }

  std::vector<ProjectTemplateCatalogEntry> entries;
  for (const auto &entry : std::filesystem::directory_iterator(m_root)) {
    if (!entry.is_directory()) {
      continue;
    }

    const auto templatePath = entry.path() / "project_template.yaml";
    if (!std::filesystem::is_regular_file(templatePath)) {
      continue;
    }

    try {
      const auto document = loadProjectTemplateDocument(templatePath);
      entries.push_back(
          {document.id, document.displayName, absoluteNormal(entry.path())});
    } catch (const std::exception &ex) {
      std::cerr << "[lxe_editor] skipping project template catalog entry "
                << templatePath << ": " << ex.what() << "\n";
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const ProjectTemplateCatalogEntry &lhs,
               const ProjectTemplateCatalogEntry &rhs) {
              return catalogEntryLess(lhs, rhs);
            });
  keepFirstEntryPerId(entries, "project template");
  m_entries = std::move(entries);
}

const std::vector<ProjectTemplateCatalogEntry> &
ProjectTemplateCatalog::entries() const {
  return m_entries;
}

std::optional<ProjectTemplateCatalogEntry>
ProjectTemplateCatalog::findById(const std::string &id) const {
  return findEntryById(m_entries, id);
}

ProjectCatalog::ProjectCatalog(std::filesystem::path root)
    : m_root(std::move(root)) {}

void ProjectCatalog::refresh() {
  if (!std::filesystem::is_directory(m_root)) {
    m_entries.clear();
    return;
  }

  std::vector<ProjectCatalogEntry> entries;
  for (const auto &entry : std::filesystem::directory_iterator(m_root)) {
    if (!entry.is_directory()) {
      continue;
    }

    const auto projectPath = entry.path() / "project.yaml";
    if (!std::filesystem::is_regular_file(projectPath)) {
      continue;
    }

    try {
      const auto document = loadProjectDocument(projectPath);
      entries.push_back(
          {document.id, document.displayName, absoluteNormal(entry.path())});
    } catch (const std::exception &ex) {
      std::cerr << "[lxe_editor] skipping project catalog entry " << projectPath
                << ": " << ex.what() << "\n";
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const ProjectCatalogEntry &lhs, const ProjectCatalogEntry &rhs) {
              return catalogEntryLess(lhs, rhs);
            });
  keepFirstEntryPerId(entries, "project");
  m_entries = std::move(entries);
}

const std::vector<ProjectCatalogEntry> &ProjectCatalog::entries() const {
  return m_entries;
}

std::optional<ProjectCatalogEntry>
ProjectCatalog::findById(const std::string &id) const {
  return findEntryById(m_entries, id);
}

std::filesystem::path
ProjectCatalog::resolveIdOrPath(const std::string &token) const {
  if (const auto entry = findById(token); entry.has_value()) {
    return entry->path;
  }

  const std::filesystem::path tokenPath(token);
  if (tokenPath.is_absolute() || hasPathSeparator(token)) {
    return absoluteNormal(tokenPath);
  }

  throw std::runtime_error("unknown project id: " + token);
}

std::filesystem::path
resolveProjectScenePath(const std::filesystem::path &projectRoot,
                        const ProjectDocument &document,
                        const std::string &sceneIdOrPath) {
  std::filesystem::path scenePath(sceneIdOrPath);
  const auto sceneIt =
      std::find_if(document.scenes.begin(), document.scenes.end(),
                   [&sceneIdOrPath](const ProjectSceneEntry &scene) {
                     return scene.id == sceneIdOrPath;
                   });
  if (sceneIt != document.scenes.end()) {
    scenePath = sceneIt->path;
  }

  const auto normalizedRoot = weaklyCanonicalNormal(projectRoot);
  const auto resolvedPath =
      scenePath.is_absolute()
          ? weaklyCanonicalNormal(scenePath)
          : weaklyCanonicalNormal(normalizedRoot / scenePath);
  if (!pathStartsWith(resolvedPath, normalizedRoot)) {
    throw std::runtime_error("project scene path escapes project root: " +
                             sceneIdOrPath);
  }
  return resolvedPath;
}

std::string makeProjectSlug(std::string name) {
  bool hasAlphaNumeric = false;
  for (char &ch : name) {
    const auto value = static_cast<unsigned char>(ch);
    ch = static_cast<char>(std::tolower(value));
    hasAlphaNumeric = hasAlphaNumeric ||
                      ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'));
    const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                      ch == '_' || ch == '-';
    if (!safe) {
      ch = '_';
    }
  }

  if (name.empty() || !hasAlphaNumeric) {
    return "project";
  }
  return name;
}

std::filesystem::path
allocateProjectPath(const std::filesystem::path &projectsRoot,
                    const std::string &requestedName) {
  const auto root = absoluteNormal(projectsRoot);
  const auto slug = makeProjectSlug(requestedName);
  auto candidate = root / slug;
  int suffix = 2;
  while (std::filesystem::exists(candidate)) {
    candidate = root / (slug + "-" + std::to_string(suffix));
    ++suffix;
  }
  return candidate.lexically_normal();
}

} // namespace LX_demo::lxe_editor
