#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct BuiltinModelAsset final {
  std::string assetId;
  std::string displayName;
  std::string category;
  std::string meshUri;
  std::string defaultMaterialUri;
  std::string albedoTextureUri;
  std::string sourcePack;
  std::string license;
  int triangleCount = 0;
  int modelBytes = 0;
  int resourceBytes = 0;
  int assetBytes = 0;
};

class BuiltinAssetCatalog final {
public:
  void refresh(const std::filesystem::path &root);
  [[nodiscard]] const std::vector<BuiltinModelAsset> &models() const {
    return m_models;
  }
  [[nodiscard]] std::optional<BuiltinModelAsset>
  findByAssetId(const std::string &assetId) const;
  [[nodiscard]] std::optional<BuiltinModelAsset>
  findByMeshUri(const std::string &meshUri) const;

private:
  std::vector<BuiltinModelAsset> m_models;
};

} // namespace LX_demo::lxe_editor
