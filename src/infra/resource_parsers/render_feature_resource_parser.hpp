#pragma once

#include "core/asset/render_effect.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

struct ParsedRenderFeatureResource final {
  std::optional<LX_core::RenderFeature> renderFeature;
  std::vector<std::string> diagnostics;
};

class RenderFeatureResourceParser final {
public:
  [[nodiscard]] ParsedRenderFeatureResource
  parse(const LX_core::ResourceUri &uri, std::string_view yamlText) const;
};

} // namespace LX_infra
