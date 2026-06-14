#include "core/frame_graph/render_validation_contract.hpp"

namespace LX_core {

RenderInputValidationResult
validatePreparedRenderInputs(const std::vector<RenderInputDesc> &descs) {
  RenderInputValidationResult result;
  result.ok = true;

  for (const RenderInputDesc &desc : descs) {
    if (!desc.accepted()) {
      result.ok = false;
    }
    for (const RenderInputDiagnostic &diagnostic : desc.diagnostics) {
      result.diagnostics.push_back(diagnostic);
      result.ok = false;
    }
  }

  return result;
}

} // namespace LX_core
