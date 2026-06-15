#include "builtin_asset_catalog.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <system_error>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string readString(const YAML::Node &node, const char *key) {
  const auto value = node[key];
  return value ? value.as<std::string>() : std::string{};
}

[[nodiscard]] int readInt(const YAML::Node &node, const char *key) {
  const auto value = node[key];
  return value ? value.as<int>() : 0;
}

[[nodiscard]] bool isAssetManifest(const std::filesystem::path &path) {
  return path.filename() == "asset.yaml";
}

} // namespace

void BuiltinAssetCatalog::refresh(const std::filesystem::path &root) {
  m_models.clear();
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    return;
  }

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root, error)) {
    if (error || !entry.is_regular_file() || !isAssetManifest(entry.path())) {
      continue;
    }
    YAML::Node node;
    try {
      node = YAML::LoadFile(entry.path().string());
    } catch (...) {
      continue;
    }
    BuiltinModelAsset asset;
    asset.assetId = readString(node, "assetId");
    asset.displayName = readString(node, "displayName");
    asset.category = readString(node, "category");
    asset.meshUri = readString(node, "meshUri");
    asset.defaultMaterialUri = readString(node, "defaultMaterialUri");
    asset.albedoTextureUri = readString(node, "albedoTextureUri");
    asset.sourcePack = readString(node, "sourcePack");
    asset.license = readString(node, "license");
    asset.triangleCount = readInt(node, "triangleCount");
    asset.modelBytes = readInt(node, "modelBytes");
    asset.resourceBytes = readInt(node, "resourceBytes");
    asset.assetBytes = readInt(node, "assetBytes");
    if (!asset.assetId.empty() && !asset.meshUri.empty()) {
      m_models.push_back(std::move(asset));
    }
  }

  std::sort(m_models.begin(), m_models.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.category != rhs.category) {
                return lhs.category < rhs.category;
              }
              return lhs.displayName < rhs.displayName;
            });
}

std::optional<BuiltinModelAsset>
BuiltinAssetCatalog::findByAssetId(const std::string &assetId) const {
  const auto it = std::find_if(
      m_models.begin(), m_models.end(),
      [&](const BuiltinModelAsset &asset) { return asset.assetId == assetId; });
  if (it == m_models.end()) {
    return std::nullopt;
  }
  return *it;
}

std::optional<BuiltinModelAsset>
BuiltinAssetCatalog::findByMeshUri(const std::string &meshUri) const {
  const auto it = std::find_if(
      m_models.begin(), m_models.end(),
      [&](const BuiltinModelAsset &asset) { return asset.meshUri == meshUri; });
  if (it == m_models.end()) {
    return std::nullopt;
  }
  return *it;
}

} // namespace LX_demo::lxe_editor
