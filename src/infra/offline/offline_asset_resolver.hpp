#pragma once

#include <filesystem>
#include <string>

namespace LX_infra::offline {

class OfflineAssetResolver final {
public:
  explicit OfflineAssetResolver(std::filesystem::path scenePath = {});

  [[nodiscard]] std::filesystem::path resolve(const std::string &uri) const;
  [[nodiscard]] const std::filesystem::path &repoRoot() const { return m_repoRoot; }

private:
  std::filesystem::path m_scenePath;
  std::filesystem::path m_repoRoot;
  std::filesystem::path m_cacheRoot;
};

} // namespace LX_infra::offline
