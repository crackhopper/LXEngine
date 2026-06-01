#include "infra/offline/offline_asset_resolver.hpp"

#include <cstdlib>
#include <stdexcept>

namespace LX_infra::offline {
namespace {

[[nodiscard]] bool hasPrefix(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

[[nodiscard]] std::filesystem::path findRepoRoot(std::filesystem::path start) {
  if (start.empty()) {
    start = std::filesystem::current_path();
  }
  if (std::filesystem::is_regular_file(start)) {
    start = start.parent_path();
  }
  for (std::filesystem::path current = std::filesystem::absolute(start);
       !current.empty(); current = current.parent_path()) {
    if (std::filesystem::exists(current / "assets") &&
        std::filesystem::exists(current / "CMakeLists.txt")) {
      return current;
    }
    if (current == current.root_path()) {
      break;
    }
  }
  return std::filesystem::current_path();
}

} // namespace

OfflineAssetResolver::OfflineAssetResolver(std::filesystem::path scenePath)
    : m_scenePath(std::move(scenePath)), m_repoRoot(findRepoRoot(m_scenePath)) {
  if (const char *cacheEnv = std::getenv("LXENGINE_ASSET_CACHE");
      cacheEnv != nullptr && *cacheEnv != '\0') {
    m_cacheRoot = cacheEnv;
  } else {
    m_cacheRoot = m_repoRoot / ".asset_cache";
  }
}

std::filesystem::path OfflineAssetResolver::resolve(const std::string &uri) const {
  if (uri.empty()) {
    throw std::runtime_error("asset URI must be non-empty");
  }
  if (hasPrefix(uri, "cache://")) {
    return m_cacheRoot / uri.substr(std::string("cache://").size());
  }
  const std::filesystem::path path(uri);
  if (path.is_absolute()) {
    return path;
  }
  if (std::filesystem::exists(m_repoRoot / path)) {
    return m_repoRoot / path;
  }
  if (!m_scenePath.empty()) {
    const std::filesystem::path sceneRelative = m_scenePath.parent_path() / path;
    if (std::filesystem::exists(sceneRelative)) {
      return sceneRelative;
    }
  }
  return m_repoRoot / path;
}

} // namespace LX_infra::offline
