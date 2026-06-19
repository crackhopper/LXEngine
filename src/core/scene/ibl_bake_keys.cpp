#include "core/scene/ibl_bake_keys.hpp"

namespace LX_core {

bool isSupportedMaterialIblBakeType(std::string_view type) {
  return type == "standard-pbr";
}

std::string materialIblBakeModelForType(std::string_view type) {
  if (type == "standard-pbr") {
    return "ggx-smith";
  }
  return {};
}

std::string_view
environmentIblBakeSourceKindName(EnvironmentIblBakeSourceKind kind) {
  switch (kind) {
  case EnvironmentIblBakeSourceKind::Equirect2D:
    return "equirect2D";
  case EnvironmentIblBakeSourceKind::TextureCube:
    return "textureCube";
  }
  return "equirect2D";
}

EnvironmentIblBakeSourceKind
environmentIblBakeSourceKindFromFeatureKind(std::string_view kind) {
  if (kind == "textureCube") {
    return EnvironmentIblBakeSourceKind::TextureCube;
  }
  return EnvironmentIblBakeSourceKind::Equirect2D;
}

} // namespace LX_core
