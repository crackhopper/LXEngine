#pragma once

#include "core/scene/scene_resource_table.hpp"
#include "core/asset/shader.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace LX_infra::offline {

using OfflineShaderProvider = std::function<LX_core::IShaderSharedPtr()>;

struct OfflineLoadedScene final {
  LX_core::SceneResourceTable table;
  LX_core::IShaderSharedPtr offlineShader;
  std::vector<std::string> warnings;
};

class OfflineSceneLoader final {
public:
  explicit OfflineSceneLoader(OfflineAssetResolver resolver,
                              OfflineShaderProvider offlineShaderProvider = {});

  [[nodiscard]] OfflineLoadedScene
  load(const LX_infra::scene_io::SceneDocument &document,
       const std::string &cameraPath) const;

  [[nodiscard]] OfflineLoadedScene
  loadFile(const std::filesystem::path &path,
           const std::string &cameraPath) const;

private:
  OfflineAssetResolver m_resolver;
  OfflineShaderProvider m_offlineShaderProvider;
};

} // namespace LX_infra::offline
