#pragma once

#include "core/asset/render_effect.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

struct ParsedRenderPathGraphResource final {
  std::optional<LX_core::RenderPathGraph> renderPathGraph;
  std::vector<std::string> diagnostics;
};

class RenderPathGraphResourceParser final {
public:
  [[nodiscard]] ParsedRenderPathGraphResource
  parse(const LX_core::ResourceUri &uri, std::string_view yamlText) const;
};

} // namespace LX_infra
