#pragma once

#include "core/asset/render_effect.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

struct ParsedRenderEffectResource final {
  std::optional<LX_core::RenderPathGraph> renderPathGraph;
  std::optional<LX_core::RenderFeature> renderFeature;
  // Historical compatibility field. New resources should use renderPathGraph
  // or renderFeature.
  std::optional<LX_core::RenderEffect> effect;
  std::vector<std::string> diagnostics;
};

class RenderEffectResourceParser final {
public:
  [[nodiscard]] ParsedRenderEffectResource
  parse(const LX_core::ResourceUri &uri, std::string_view yamlText) const;
};

} // namespace LX_infra
