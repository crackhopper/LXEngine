#pragma once

#include "core/scene/scene_resource_table.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace LX_infra::offline {

struct OfflineLoadedScene final {
  LX_core::SceneResourceTable table;
  std::vector<std::string> warnings;
};

class OfflineSceneLoader final {
public:
  explicit OfflineSceneLoader(OfflineAssetResolver resolver);

  [[nodiscard]] OfflineLoadedScene
  load(const LX_infra::scene_io::SceneDocument &document,
       const std::string &cameraPath) const;

  [[nodiscard]] OfflineLoadedScene
  loadFile(const std::filesystem::path &path,
           const std::string &cameraPath) const;

private:
  OfflineAssetResolver m_resolver;
};

} // namespace LX_infra::offline
