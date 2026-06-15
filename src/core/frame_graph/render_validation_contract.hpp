#pragma once

#include "core/frame_graph/render_input.hpp"

#include <vector>

namespace LX_core {

struct RenderInputValidationResult final {
  bool ok = false;
  std::vector<RenderInputDiagnostic> diagnostics;
};

[[nodiscard]] RenderInputValidationResult
validatePreparedRenderInputs(const std::vector<RenderInputDesc> &descs);

} // namespace LX_core
