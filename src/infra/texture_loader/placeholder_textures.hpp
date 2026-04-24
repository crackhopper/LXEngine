#pragma once

#include "core/asset/texture.hpp"

#include <string>

namespace LX_infra {

/// Lazily-created singleton placeholder textures.
LX_core::CombinedTextureSamplerSharedPtr getPlaceholderWhite();
LX_core::CombinedTextureSamplerSharedPtr getPlaceholderBlack();
LX_core::CombinedTextureSamplerSharedPtr getPlaceholderNormal();

/// Resolve a placeholder name ("white", "black", "normal") to the
/// corresponding texture, or nullptr if the name is not a placeholder.
LX_core::CombinedTextureSamplerSharedPtr
resolvePlaceholder(const std::string &name);

} // namespace LX_infra
