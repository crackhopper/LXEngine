#pragma once

#include "core/frame_graph/render_queue.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct BindlessValidationDiagnostic final {
  usize itemIndex = 0;
  StringID pass;
  StringID debugId;
  std::string reason;
};

struct BindlessValidationResult final {
  bool ok = false;
  usize coveredItemCount = 0;
  std::vector<BindlessValidationDiagnostic> diagnostics;
};

[[nodiscard]] BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass);

} // namespace LX_core
