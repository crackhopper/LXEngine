#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

enum class SceneSourceKind {
  Asset,
  Local,
};

struct SceneCatalogRoots final {
  std::vector<std::filesystem::path> assetRoots;
  std::vector<std::filesystem::path> localRoots;
};

struct SceneCatalogEntry final {
  std::string id;
  std::string displayName;
  SceneSourceKind kind = SceneSourceKind::Local;
  std::filesystem::path path;
};

class SceneCatalog final {
public:
  explicit SceneCatalog(SceneCatalogRoots roots);

  void refresh();
  const std::vector<SceneCatalogEntry>& entries() const;

  std::optional<SceneCatalogEntry> findById(const std::string& id) const;
  std::optional<SceneCatalogEntry>
  classifyPath(const std::filesystem::path& path) const;
  std::filesystem::path resolveNameOrPath(const std::string& token) const;

private:
  SceneCatalogRoots m_roots;
  std::vector<SceneCatalogEntry> m_entries;
};

} // namespace LX_demo::lxe_editor
