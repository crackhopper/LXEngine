#pragma once

#include "core/asset/render_effect.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <string>
#include <vector>

namespace LX_infra {

struct MaterialSourceVariantResolverResult final {
  bool success = false;
  usize resolvedVariantCount = 0;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialSourceVariantResolverResult
resolveMaterialSourceVariants(LX_core::SceneResourceTable &table,
                              const LX_core::RenderPathGraph &graph,
                              const LX_core::ResourceUri &graphUri);

} // namespace LX_infra
