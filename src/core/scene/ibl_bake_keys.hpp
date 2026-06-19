#pragma once

#include "core/resource/resource_uri.hpp"
#include "core/scene/ibl_bake_types.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace LX_core {

struct EnvironmentIblBakeKey final {
  ResourceUri environmentMapUri;
  std::string sourceHash;
  EnvironmentIblBakeSourceKind sourceKind =
      EnvironmentIblBakeSourceKind::Equirect2D;
  bool operator==(const EnvironmentIblBakeKey &) const = default;
};

struct MaterialIblBakeKey final {
  std::string materialType;
  std::string bsdfModel;
  bool operator==(const MaterialIblBakeKey &) const = default;
};

enum class IblBakeItemKind {
  EnvironmentLight,
  MaterialBrdf,
};

struct IblBakeItem final {
  BakeItemId id = 0;
  IblBakeItemKind kind = IblBakeItemKind::EnvironmentLight;
  std::variant<EnvironmentIblBakeKey, MaterialIblBakeKey> key;
  ResourceUri bakeRenderPathUri;
};

struct IblBakeItemCollection final {
  std::vector<IblBakeItem> items;
  std::vector<IblBakeItem> environmentItems;
  std::vector<IblBakeItem> materialItems;
  std::vector<IblBakeJobEvent> warnings;
};

[[nodiscard]] bool isSupportedMaterialIblBakeType(std::string_view type);
[[nodiscard]] std::string materialIblBakeModelForType(std::string_view type);
[[nodiscard]] std::string_view
environmentIblBakeSourceKindName(EnvironmentIblBakeSourceKind kind);
[[nodiscard]] EnvironmentIblBakeSourceKind
environmentIblBakeSourceKindFromFeatureKind(std::string_view kind);

} // namespace LX_core
