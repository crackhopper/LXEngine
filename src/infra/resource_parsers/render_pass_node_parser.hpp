#pragma once

#include "core/asset/render_effect.hpp"

#include <optional>
#include <string>
#include <vector>

namespace YAML {
class Node;
} // namespace YAML

namespace LX_infra {

struct RenderPassNodeParseResult final {
  std::optional<LX_core::RenderPassNode> pass;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] RenderPassNodeParseResult
parseRenderPassNodeContract(const std::string &passName, const YAML::Node &node,
                            const std::string &fieldPrefix);

} // namespace LX_infra
