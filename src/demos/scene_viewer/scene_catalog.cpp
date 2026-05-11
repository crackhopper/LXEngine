#include "demos/scene_viewer/scene_catalog.hpp"

#include <algorithm>
#include <stdexcept>

namespace LX_demo::scene_viewer {
namespace {

[[nodiscard]] std::filesystem::path normalizePath(
    const std::filesystem::path& path) {
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] bool isUnderRoot(const std::filesystem::path& path,
                               const std::filesystem::path& root) {
  const std::filesystem::path normalizedPath = normalizePath(path);
  const std::filesystem::path normalizedRoot = normalizePath(root);

  auto pathIt = normalizedPath.begin();
  auto rootIt = normalizedRoot.begin();
  for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt) {
    if (pathIt == normalizedPath.end() || *pathIt != *rootIt) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string makeRelativeId(const std::filesystem::path& path,
                                         const std::filesystem::path& root) {
  return std::filesystem::relative(path, root).generic_string();
}

void scanRoot(std::vector<SceneCatalogEntry>& out, const std::filesystem::path& root,
              const SceneSourceKind kind) {
  if (!std::filesystem::exists(root)) {
    return;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto filename = entry.path().filename().string();
    if (filename.size() < std::string(".scene.yaml").size() ||
        entry.path().extension() != ".yaml") {
      continue;
    }

    SceneCatalogEntry catalogEntry;
    catalogEntry.id = makeRelativeId(entry.path(), root);
    catalogEntry.displayName = entry.path().stem().stem().string();
    catalogEntry.kind = kind;
    catalogEntry.path = normalizePath(entry.path());
    out.push_back(std::move(catalogEntry));
  }
}

} // namespace

SceneCatalog::SceneCatalog(SceneCatalogRoots roots) : m_roots(std::move(roots)) {}

void SceneCatalog::refresh() {
  m_entries.clear();
  for (const auto& root : m_roots.assetRoots) {
    scanRoot(m_entries, root, SceneSourceKind::Asset);
  }
  for (const auto& root : m_roots.localRoots) {
    scanRoot(m_entries, root, SceneSourceKind::Local);
  }
  std::sort(m_entries.begin(), m_entries.end(),
            [](const SceneCatalogEntry& lhs, const SceneCatalogEntry& rhs) {
              if (lhs.kind != rhs.kind) {
                return lhs.kind == SceneSourceKind::Asset;
              }
              return lhs.id < rhs.id;
            });
}

const std::vector<SceneCatalogEntry>& SceneCatalog::entries() const {
  return m_entries;
}

std::optional<SceneCatalogEntry>
SceneCatalog::findById(const std::string& id) const {
  const auto it = std::find_if(
      m_entries.begin(), m_entries.end(),
      [&id](const SceneCatalogEntry& entry) { return entry.id == id; });
  if (it == m_entries.end()) {
    return std::nullopt;
  }
  return *it;
}

std::optional<SceneCatalogEntry>
SceneCatalog::classifyPath(const std::filesystem::path& path) const {
  if (path.empty()) {
    return std::nullopt;
  }

  const std::filesystem::path normalized = normalizePath(path);
  for (const auto& root : m_roots.assetRoots) {
    if (isUnderRoot(normalized, root)) {
      return SceneCatalogEntry{
          .id = makeRelativeId(normalized, normalizePath(root)),
          .displayName = normalized.stem().stem().string(),
          .kind = SceneSourceKind::Asset,
          .path = normalized,
      };
    }
  }
  for (const auto& root : m_roots.localRoots) {
    if (isUnderRoot(normalized, root)) {
      return SceneCatalogEntry{
          .id = makeRelativeId(normalized, normalizePath(root)),
          .displayName = normalized.stem().stem().string(),
          .kind = SceneSourceKind::Local,
          .path = normalized,
      };
    }
  }
  return std::nullopt;
}

std::filesystem::path SceneCatalog::resolveNameOrPath(const std::string& token) const {
  if (const auto entry = findById(token); entry.has_value()) {
    return entry->path;
  }

  const std::filesystem::path pathToken = token;
  if (pathToken.is_absolute() || token.find('/') != std::string::npos ||
      token.find('\\') != std::string::npos) {
    return normalizePath(pathToken);
  }

  throw std::runtime_error("scene not found in catalog: " + token);
}

} // namespace LX_demo::scene_viewer
