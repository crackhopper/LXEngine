#pragma once

#include "core/asset/render_effect.hpp"
#include "core/asset/shader.hpp"

#include <string>
#include <vector>

namespace LX_core {

enum class RenderFeatureShaderValidationSeverity {
  Error,
};

struct RenderFeatureShaderValidationDiagnostic final {
  RenderFeatureShaderValidationSeverity severity =
      RenderFeatureShaderValidationSeverity::Error;
  std::string parameter;
  std::string message;
};

[[nodiscard]] std::vector<RenderFeatureShaderValidationDiagnostic>
validateRenderFeatureShaderAbi(const RenderFeature &feature,
                               const IShader &shader);

} // namespace LX_core
