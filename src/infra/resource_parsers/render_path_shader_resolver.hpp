#pragma once

#include "core/resource/resource_uri.hpp"

#include <string>
#include <vector>

namespace LX_infra {

struct RenderPathShaderSourceResolveResult final {
  std::vector<LX_core::ResourceUri> sourceUris;
  std::vector<std::string> diagnostics;

  [[nodiscard]] bool success() const {
    return !sourceUris.empty() && diagnostics.empty();
  }
};

[[nodiscard]] RenderPathShaderSourceResolveResult
resolveRenderPathShaderSourceUris(const LX_core::ResourceUri &graphUri,
                                  const std::string &passId,
                                  const LX_core::ResourceUri &shaderUri);

} // namespace LX_infra
