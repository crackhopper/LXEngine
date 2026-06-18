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

} // namespace LX_core
